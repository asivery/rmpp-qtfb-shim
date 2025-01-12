#include "input-shim.h"
#include "shim.h"
#include <algorithm>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <asm/ioctl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>

#define BIT(x) (1 << x)

#define REAL_TOUCHSCREEN "/dev/input/event3"
#define REAL_DIGITIZER "/dev/input/event2"

#define RM2_TOUCHSCREEN "/dev/input/event2"
#define RM2_DIGITIZER "/dev/input/event1"
#define RM2_TOUCHSCREEN_2 "/dev/input/touchscreen0"

#define RM2_MAX_DIGI_X 20967
#define RM2_MAX_DIGI_Y 15725
#define RMPP_MAX_DIGI_X 11180
#define RMPP_MAX_DIGI_Y 15340

#define RM2_MAX_TOUCH_X 20967
#define RM2_MAX_TOUCH_Y 15725
#define RM2_MAX_PRESSURE 4095

#define RM2_MAX_TOUCH_MAJOR 255
#define RM2_MAX_TOUCH_MINOR 255
#define RM2_MIN_ORIENTATION -127
#define RM2_MAX_ORIENTATION  127

#define RMPP_MAX_TOUCH_X 2064
#define RMPP_MAX_TOUCH_Y 2832
#define RMPP_MAX_PRESSURE 255


static std::vector<int> fdsTouchScreen;
static std::vector<int> fdsDigitizer;

int inputShimOpen(const char *file, int (*realOpen)(const char *name, int flags, int mode)) {
    if(strcmp(file, RM2_DIGITIZER) == 0) {
        int fd = realOpen(REAL_DIGITIZER, O_RDONLY, 0);
        CERR << "Open digitizer" << std::endl;
        fdsDigitizer.push_back(fd);
        return fd;
    }

    if(strcmp(file, RM2_TOUCHSCREEN) == 0 || strcmp(file, RM2_TOUCHSCREEN_2) == 0) {
        int fd = realOpen(REAL_TOUCHSCREEN, O_RDONLY, 0);
        CERR << "Open touchscreen" << std::endl;
        fdsTouchScreen.push_back(fd);
        return fd;
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
    int status = realIoctl(fd, request, ptr);
    if (status == -1) {
        struct input_absinfo *fake = reinterpret_cast<struct input_absinfo*>(ptr);
        std::memset(fake, 0, sizeof(fake));
        fake->value   = 0;
        fake->minimum = minVal;
        fake->maximum = maxVal;
        fake->fuzz    = fuzzVal;
        fake->flat    = flatVal;
        fake->resolution = resolutionVal;
        return 0;
    } else {
        struct input_absinfo *absinfo = reinterpret_cast<struct input_absinfo*>(ptr);
        absinfo->minimum    = minVal;
        absinfo->maximum    = maxVal;
        absinfo->fuzz       = fuzzVal;
        absinfo->flat       = flatVal;
        absinfo->resolution = resolutionVal;
        return 0;
    }
}

inline void setBit(unsigned long *bits, int code) {
    bits[code / (8 * sizeof(long))] |= (1UL << (code % (8 * sizeof(long))));
}

#define IS_MATCHING_IOCTL(dir, type, nr) ((request & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == (_IOC(dir, type, nr, 0)))
#define IS_MATCHING_IOCTL_S(dir, type, nr, size) (request == (_IOC(dir, type, nr, size)))
#define SHIM_INVOKE_ORIGINAL if((status = realIoctl(fd, request, ptr)) == -1) return status
int inputShimIoctl(int fd, unsigned long request, char *ptr, int (*realIoctl)(int fd, unsigned long request, char *ptr)) {
    int ioctlInternalSize = _IOC_SIZE(request);

    auto position = std::find(fdsTouchScreen.begin(), fdsTouchScreen.end(), fd);
    if (position != fdsTouchScreen.end()) {
        CERR << "Touchscreen IOCTL: " << request << std::endl;
        int status;

        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_MT_POSITION_X, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM2_MAX_TOUCH_X,
                                        100, 0, 0);
        }
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_MT_POSITION_Y, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM2_MAX_TOUCH_Y,
                                        100, 0, 0);
        }
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_MT_ORIENTATION, sizeof(input_absinfo))) {
            int status = realIoctl(fd, request, ptr);
            if (status < 0) {
                struct input_absinfo *fake = reinterpret_cast<struct input_absinfo*>(ptr);
                std::memset(fake, 0, sizeof(fake));
                fake->minimum = RM2_MIN_ORIENTATION;
                fake->maximum = RM2_MAX_ORIENTATION;
                return 0;
            } else {
                // override
                struct input_absinfo *absinfo = reinterpret_cast<struct input_absinfo*>(ptr);
                absinfo->minimum = RM2_MIN_ORIENTATION;
                absinfo->maximum = RM2_MAX_ORIENTATION;
                return 0;
            }
        }

        unsigned cmdDir  = _IOC_DIR(request);
        unsigned cmdType = _IOC_TYPE(request);
        unsigned cmdNr   = _IOC_NR(request);
        unsigned cmdSize = _IOC_SIZE(request);

        // pretend like we support everything the rm2 supports
        if (cmdDir == _IOC_READ && cmdType == 'E' && cmdNr == (0x20 + EV_ABS)) {
            int status = realIoctl(fd, request, ptr);
            if (status < 0) return status;

            unsigned long *bits = (unsigned long*) ptr;

            setBit(bits, ABS_MT_POSITION_X);
            setBit(bits, ABS_MT_POSITION_Y);
            setBit(bits, ABS_MT_PRESSURE);
            setBit(bits, ABS_MT_TOUCH_MAJOR);
            setBit(bits, ABS_MT_TOUCH_MINOR);
            setBit(bits, ABS_MT_ORIENTATION);
            setBit(bits, ABS_MT_SLOT);
            setBit(bits, ABS_MT_TOOL_TYPE);
            setBit(bits, ABS_MT_TRACKING_ID);

            return 0;
        }

    }

    position = std::find(fdsDigitizer.begin(), fdsDigitizer.end(), fd);
    if(position != fdsDigitizer.end()) {
        CERR << "Digitizer IOCTL: " << request << std::endl;

        int status;

        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_X, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM2_MAX_DIGI_X,
                                        0, 0, 0);
        }
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_Y, sizeof(input_absinfo))) {
            return fakeOrOverrideAbsInfo(fd, request, ptr, realIoctl,
                                        0, RM2_MAX_DIGI_Y,
                                        0, 0, 0);
        }
    }
    return INTERNAL_SHIM_NOT_APPLICABLE;
}

static int lastPressure;
#undef SHIM_INVOKE_ORIGINAL
#define SHIM_INVOKE_ORIGINAL if((size = realRead(fd, bfr, size)) < 1) return size
ssize_t inputShimRead(int fd, void *bfr, unsigned long size, ssize_t (*realRead)(int fd, void *bfr, size_t size)) {
    auto position = std::find(fdsDigitizer.begin(), fdsDigitizer.end(), fd);
    if(position != fdsDigitizer.end()) {
        struct input_event *evt = (struct input_event *) bfr;
        if((size % sizeof(input_event)) != 0) {
            CERR << "Unexpected read size: " << size << ". Expected multiple of " << sizeof(input_event) << ". Passing to normal read." << std::endl;
            return INTERNAL_SHIM_NOT_APPLICABLE;
        }
        SHIM_INVOKE_ORIGINAL;
        for(int i = 0; i<(size / sizeof(input_event)) - 1; i++) {
            switch(evt->code) {
                // Remap x to y, y to -x. Remap to real RM2 sizes
                case ABS_X:
                    evt->code = ABS_Y;
                    evt->value = ((evt->value * RM2_MAX_DIGI_Y) / RMPP_MAX_DIGI_X);
                    break;
                case ABS_Y:
                    evt->code = ABS_X;
                    evt->value = RM2_MAX_DIGI_X - ((evt->value * RM2_MAX_DIGI_X) / RMPP_MAX_DIGI_Y);
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
        return size;
    }

    position = std::find(fdsTouchScreen.begin(), fdsTouchScreen.end(), fd);
    if(position != fdsTouchScreen.end()) {
        struct input_event *evt = (struct input_event *) bfr;
        if((size % sizeof(input_event)) != 0) {
            CERR << "Unexpected read size: " << size << ". Expected multiple of " << sizeof(input_event) << ". Passing to normal read." << std::endl;
            return INTERNAL_SHIM_NOT_APPLICABLE;
        }
        SHIM_INVOKE_ORIGINAL;
        for(int i = 0; i<(size / sizeof(input_event)) - 1; i++) {
            switch (evt->code) {
                case ABS_MT_POSITION_X: {
                    int oldVal = evt->value;
                    int newVal = RM2_MAX_DIGI_X - (oldVal * RM2_MAX_DIGI_X) / RMPP_MAX_TOUCH_X;
                    evt->value = newVal;
                    break;
                }
                case ABS_MT_POSITION_Y: {
                    int oldVal = evt->value;
                    int newVal = RM2_MAX_DIGI_Y - (oldVal * RM2_MAX_DIGI_Y) / RMPP_MAX_TOUCH_Y;
                    evt->value = newVal;
                    break;
                }
                case ABS_MT_PRESSURE: {
                    int oldVal = evt->value;
                    int newVal = (oldVal * RM2_MAX_PRESSURE) / RMPP_MAX_PRESSURE;
                    evt->value = newVal;
                    break;
                }
                case ABS_MT_TOOL_TYPE: {
                    if (evt->value > 1) {
                        evt->value = 1;
                    }
                    break;
                }
            }
            evt++;
        }
        return size;
    }

    return INTERNAL_SHIM_NOT_APPLICABLE;
}

#undef SHIM_INVOKE_ORIGINAL
