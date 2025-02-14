#include "input-shim.h"
#include "shim.h"
#include <algorithm>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <asm/ioctl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>

#define BIT(x) (1ULL << x)

#define REAL_TOUCHSCREEN "/dev/input/event3"
#define REAL_DIGITIZER "/dev/input/event2"

#define RM1_TOUCHSCREEN "/dev/input/event1"
#define RM1_DIGITIZER "/dev/input/event0"
#define RM1_TOUCHSCREEN_2 "/dev/input/touchscreen0"

#define RM1_MAX_DIGI_X 20967
#define RM1_MAX_DIGI_Y 15725
#define RMPP_MAX_DIGI_X 11180
#define RMPP_MAX_DIGI_Y 15340

#define RM1_MAX_TOUCH_X 767
#define RM1_MAX_TOUCH_Y 1023
#define RM1_MAX_PRESSURE 4095

#define RM1_MAX_TOUCH_MAJOR 255
#define RM1_MAX_TOUCH_MINOR 255
#define RM1_MIN_ORIENTATION -127
#define RM1_MAX_ORIENTATION  127

#define RMPP_MAX_TOUCH_X 2064
#define RMPP_MAX_TOUCH_Y 2832
#define RMPP_MAX_PRESSURE 255

const char *UNBOUND_INPUTS[] = { "/dev/input/event3", "/dev/input/event2", };


static std::vector<int> fdsTouchScreen;
static std::vector<int> fdsDigitizer;

int inputShimOpen(const char *file, int (*realOpen)(const char *name, int flags, mode_t mode), int flags, mode_t mode) {
    if(strcmp(file, RM1_DIGITIZER) == 0) {
        int fd = realOpen(REAL_DIGITIZER, flags, 0);
        CERR << "Open digitizer" << std::endl;
        fdsDigitizer.push_back(fd);
        return fd;
    }

    if(strcmp(file, RM1_TOUCHSCREEN) == 0 || strcmp(file, RM1_TOUCHSCREEN_2) == 0) {
        int fd = realOpen(REAL_TOUCHSCREEN, flags, 0);
        CERR << "Open touchscreen" << std::endl;
        fdsTouchScreen.push_back(fd);
        return fd;
    }

    for(int i = 0; i < sizeof(UNBOUND_INPUTS) / sizeof(const char *); i++) {
        if(strcmp(file, UNBOUND_INPUTS[i]) == 0) {
            return realOpen("/dev/input/event0", flags, 0);
        }
    }

    return INTERNAL_SHIM_NOT_APPLICABLE;
}

int inputShimClose(int fd, int (*realClose)(int fd)) {
    auto position = std::find(fdsTouchScreen.begin(), fdsTouchScreen.end(), fd);
    if(position != fdsTouchScreen.end()) {
        fdsTouchScreen.erase(position);
        return realClose(fd);
    }

    position = std::find(fdsDigitizer.begin(), fdsDigitizer.end(), fd);
    if(position != fdsDigitizer.end()) {
        fdsDigitizer.erase(position);
        return realClose(fd);
    }

    return INTERNAL_SHIM_NOT_APPLICABLE;
}

static int fakeOrOverrideAbsInfo(
    int fd,
    unsigned long request,
    char *ptr,
    int (*realIoctl)(int, unsigned long, char*),
    int minVal,
    int maxVal,
    int fuzzVal,
    int flatVal,
    int resolutionVal)
{
    // Disregard original return value.
    struct input_absinfo *absinfo = reinterpret_cast<struct input_absinfo*>(ptr);
    std::memset(absinfo, 0, sizeof(absinfo));

    realIoctl(fd, request, ptr);
    absinfo->minimum    = minVal;
    absinfo->maximum    = maxVal;
    absinfo->fuzz       = fuzzVal;
    absinfo->flat       = flatVal;
    absinfo->resolution = resolutionVal;
    return 0;
}

#define IS_MATCHING_IOCTL(dir, type, nr) ((request & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == (_IOC(dir, type, nr, 0)))
#define IS_MATCHING_IOCTL_S(dir, type, nr, size) (request == (_IOC(dir, type, nr, size)))
#define SHIM_INVOKE_ORIGINAL if((status = realIoctl(fd, request, ptr)) == -1) return status
int inputShimIoctl(int fd, unsigned long request, char *ptr, int (*realIoctl)(int fd, unsigned long request, char *ptr)) {
    int ioctlInternalSize = _IOC_SIZE(request);

    unsigned cmdDir  = _IOC_DIR(request);
    unsigned cmdType = _IOC_TYPE(request);
    unsigned cmdNr   = _IOC_NR(request);
    unsigned cmdSize = _IOC_SIZE(request);

    auto position = std::find(fdsTouchScreen.begin(), fdsTouchScreen.end(), fd);
    if (position != fdsTouchScreen.end()) {
        CERR << "Touchscreen IOCTL: " << request << std::endl;
        int status;

        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_MT_POSITION_X, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM1_MAX_TOUCH_X,
                                        100, 0, 0);
        }
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_MT_POSITION_Y, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM1_MAX_TOUCH_Y,
                                        100, 0, 0);
        }
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_MT_ORIENTATION, sizeof(input_absinfo))) {
            struct input_absinfo *absinfo = reinterpret_cast<struct input_absinfo*>(ptr);
            std::memset(absinfo, 0, sizeof(absinfo));
            int status = realIoctl(fd, request, ptr);
            absinfo->minimum = RM1_MIN_ORIENTATION;
            absinfo->maximum = RM1_MAX_ORIENTATION;
            return 0;
        }

        if(IS_MATCHING_IOCTL(_IOC_READ, 'E', 0x6)) {
            // Get Name
            CERR << "Get device name" << std::endl;
            strncpy(ptr, "cyttsp5_mt", cmdSize);
            return 0;
        }

        // pretend like we support everything the RM1 supports
        if (cmdDir == _IOC_READ && cmdType == 'E' && cmdNr == 0x20) {
            int status = realIoctl(fd, request, ptr);
            if (status < 0) return status;

            unsigned long *bits = (unsigned long*) ptr;
            *bits |= BIT(EV_ABS);
            *bits |= BIT(EV_REL);
            return 0;
        }

        if (cmdDir == _IOC_READ && cmdType == 'E' && cmdNr == (0x20 + EV_ABS)) {
            int status = realIoctl(fd, request, ptr);
            if (status < 0) return status;

            unsigned long *bits = (unsigned long*) ptr;

            *bits |= BIT(ABS_MT_POSITION_X);
            *bits |= BIT(ABS_MT_POSITION_Y);
            *bits |= BIT(ABS_MT_PRESSURE);
            *bits |= BIT(ABS_MT_TOUCH_MAJOR);
            *bits |= BIT(ABS_MT_TOUCH_MINOR);
            *bits |= BIT(ABS_MT_ORIENTATION);
            *bits |= BIT(ABS_MT_SLOT);
            *bits |= BIT(ABS_MT_TOOL_TYPE);
            *bits |= BIT(ABS_MT_TRACKING_ID);

            return 0;
        }

        return realIoctl(fd, request, ptr);
    }

    position = std::find(fdsDigitizer.begin(), fdsDigitizer.end(), fd);
    if(position != fdsDigitizer.end()) {
        CERR << "Digitizer IOCTL: " << request << std::endl;

        int status;

        if(IS_MATCHING_IOCTL(_IOC_READ, 'E', 0x6)) {
            // Get Name
            CERR << "Get device name" << std::endl;
            strncpy(ptr, "Wacom I2C Digitizer", cmdSize);
            return 0;
        }

        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_X, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM1_MAX_DIGI_X,
                                        0, 0, 0);
        }
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_Y, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM1_MAX_DIGI_Y,
                                        0, 0, 0);
        }
    }
    return INTERNAL_SHIM_NOT_APPLICABLE;
}

static int lastPressure;
#undef SHIM_INVOKE_ORIGINAL
#define SHIM_INVOKE_ORIGINAL if((read = realRead(fd, bfr, orgSize)) < 1) return read
#define INPUT_BUFFER_SIZE 1024
static struct input_event circularBuffer[INPUT_BUFFER_SIZE];
int writingPosition = 0;
int readingPosition = 0;

int readFromBuffer(int size, void *bfr){
    int countToWrite = (size / sizeof(struct input_event)) - 1;
    int countWritten = 0;
    while(readingPosition != writingPosition && countToWrite > 0) {
        memcpy(&((struct input_event *) bfr)[countWritten], &circularBuffer[readingPosition], sizeof(struct input_event));
        countToWrite--;
        countWritten++;
        readingPosition++;
        if(readingPosition >= INPUT_BUFFER_SIZE) readingPosition = 0;
    }
    if(countWritten) {
        // Abort and wait for the next turn
        memset(&((struct input_event *) bfr)[countWritten], 0, sizeof(struct input_event));
        return (countWritten + 1) * sizeof(struct input_event);
    }
    return -1;
}

ssize_t inputShimRead(int fd, void *bfr, unsigned long orgSize, ssize_t (*realRead)(int fd, void *bfr, size_t size)) {
    auto position = std::find(fdsDigitizer.begin(), fdsDigitizer.end(), fd);
    int read; // for macro use
    if(position != fdsDigitizer.end()) {
        struct input_event *evt = (struct input_event *) bfr;
        if((orgSize % sizeof(input_event)) != 0) {
            CERR << "Unexpected read size: " << orgSize << ". Expected multiple of " << sizeof(input_event) << ". Passing to normal read." << std::endl;
            return INTERNAL_SHIM_NOT_APPLICABLE;
        }
        SHIM_INVOKE_ORIGINAL;
        for(int i = 0; i<(read / sizeof(input_event)) - 1; i++) {
            switch(evt->code) {
                // Remap x to y, y to -x. Remap to real RM1 sizes
                case ABS_X:
                    evt->code = ABS_Y;
                    evt->value = ((evt->value * RM1_MAX_DIGI_Y) / RMPP_MAX_DIGI_X);
                    break;
                case ABS_Y:
                    evt->code = ABS_X;
                    evt->value = RM1_MAX_DIGI_X - ((evt->value * RM1_MAX_DIGI_X) / RMPP_MAX_DIGI_Y);
                    break;
                case ABS_PRESSURE:
                    lastPressure = evt->value;
                    break;
                case ABS_DISTANCE:
                    if(lastPressure != 0) {
                        evt->value = 0;
                    }
                    break;
            }
            evt++;
        }
        return read;
    }

    position = std::find(fdsTouchScreen.begin(), fdsTouchScreen.end(), fd);
    if(position != fdsTouchScreen.end()) {
        // The buffer is empty.
        struct input_event *evt = (struct input_event *) bfr;
        if((orgSize % sizeof(input_event)) != 0) {
            CERR << "Unexpected read size: " << orgSize << ". Expected multiple of " << sizeof(input_event) << ". Passing to normal read." << std::endl;
            return INTERNAL_SHIM_NOT_APPLICABLE;
        }
        SHIM_INVOKE_ORIGINAL;
        CERR << "Invoking read on touch panel. Size = " << read << std::endl;
        for(int i = 0; i<(read / sizeof(input_event)); i++) {
            CERR << "Touch event: " << evt->code << ", value: " << evt->value << ", type: " << evt->type << std::endl;
            switch (evt->code) {
                case ABS_MT_POSITION_X: {
                    int oldVal = evt->value;
                    int newVal = RM1_MAX_TOUCH_X - (oldVal * RM1_MAX_TOUCH_X) / RMPP_MAX_TOUCH_X;
                    evt->value = newVal;
                    CERR << "Remapped to: " << evt->value << std::endl;
                    break;
                }
                case ABS_MT_POSITION_Y: {
                    int oldVal = evt->value;
                    int newVal = RM1_MAX_TOUCH_Y - (oldVal * RM1_MAX_TOUCH_Y) / RMPP_MAX_TOUCH_Y;
                    evt->value = newVal;
                    CERR << "Remapped to: " << evt->value << std::endl;
                    break;
                }
                case ABS_MT_PRESSURE: {
                    int oldVal = evt->value;
                    int newVal = (oldVal * RM1_MAX_PRESSURE) / RMPP_MAX_PRESSURE;
                    evt->value = newVal;
                    CERR << "Remapped to: " << evt->value << std::endl;
                    break;
                }
                case ABS_MT_TOOL_TYPE: {
                    if (evt->value > 1) {
                        evt->value = 1;
                    }
                    CERR << "Remapped to: " << evt->value << std::endl;
                    break;
                }
            }
            evt++;
        }
        return read;
    }

    return INTERNAL_SHIM_NOT_APPLICABLE;
}

#undef SHIM_INVOKE_ORIGINAL
