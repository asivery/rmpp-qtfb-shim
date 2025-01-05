#include "input-shim.h"
#include "shim.h"
#include <algorithm>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <asm/ioctl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#define REAL_TOUCHSCREEN "/dev/input/event3"
#define REAL_DIGITIZER "/dev/input/event2"

#define RM2_TOUCHSCREEN "/dev/input/event2"
#define RM2_DIGITIZER "/dev/input/event1"
#define RM2_TOUCHSCREEN_2 "/dev/input/touchscreen0"

#define RM2_MAX_DIGI_X 20967
#define RM2_MAX_DIGI_Y 15725
#define RMPP_MAX_DIGI_X 11180
#define RMPP_MAX_DIGI_Y 15340

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

#define IS_MATCHING_IOCTL(dir, type, nr) ((request & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == (_IOC(dir, type, nr, 0)))
#define IS_MATCHING_IOCTL_S(dir, type, nr, size) (request == (_IOC(dir, type, nr, size)))
#define SHIM_INVOKE_ORIGINAL if((status = realIoctl(fd, request, ptr)) == -1) return status
int inputShimIoctl(int fd, unsigned long request, char *ptr, int (*realIoctl)(int fd, unsigned long request, char *ptr)) {
    int ioctlInternalSize = _IOC_SIZE(request);

    auto position = std::find(fdsTouchScreen.begin(), fdsTouchScreen.end(), fd);
    if(position != fdsTouchScreen.end()) {
        CERR << "Touch IOCTL: " << request << " " << std::endl;
        int status;
        if(IS_MATCHING_IOCTL(_IOC_READ, 'E', 0x20)) {
            CERR << "CAPS? " << ioctlInternalSize << std::endl;
            SHIM_INVOKE_ORIGINAL;
            int *a = (int *) ptr;
            CERR << status << " " << *a << std::endl;
            // Fake the touchscreen capabilities
            // TODO: Touch is broken
            *a |= (1 << 3);
            *a |= (1 << 2);
            return status;
        }
    }

    position = std::find(fdsDigitizer.begin(), fdsDigitizer.end(), fd);
    if(position != fdsDigitizer.end()) {
        CERR << "Digitizer IOCTL: " << request << std::endl;

        int status;
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_X, sizeof(input_absinfo))) {
            SHIM_INVOKE_ORIGINAL;
            CERR << "Query max X. Size: " << ioctlInternalSize << std::endl;
            struct input_absinfo *org = (struct input_absinfo *) ptr;
            org->maximum = RM2_MAX_DIGI_X;
            CERR << "Set: " << org->maximum << std::endl;
            return 0;
        }
        if (IS_MATCHING_IOCTL_S(_IOC_READ, 'E', 0x40 + ABS_Y, sizeof(input_absinfo))) {
            SHIM_INVOKE_ORIGINAL;
            CERR << "Query max Y. Size: " << ioctlInternalSize << std::endl;
            struct input_absinfo *org = (struct input_absinfo *) ptr;
            org->maximum = RM2_MAX_DIGI_Y;
            CERR << "Set: " << org->maximum << std::endl;
            return 0;
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

    return INTERNAL_SHIM_NOT_APPLICABLE;
}

#undef SHIM_INVOKE_ORIGINAL
