#include <stdbool.h>
#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include "shim.h"
#include "fb-shim.h"
#include "input-shim.h"
#include <sys/mman.h>
#include "qtfb-client/common.h"

#define FAKE_MODEL "reMarkable 1.0"
#define FILE_MODEL "/sys/devices/soc0/machine"

bool shimModel;
bool shimInput;
bool shimFramebuffer;

qtfb::FBKey shimFramebufferKey = QTFB_DEFAULT_FRAMEBUFFER;
uint8_t shimType = FBFMT_RM2FB;

bool readEnvvarBoolean(const char *name, bool _default) {
    char *value = getenv(name);
    if(value == NULL) {
        return _default;
    }
    return strcmp(value, "1") == 0;
}

void __attribute__((constructor)) __construct () {
    shimModel = readEnvvarBoolean("QTFB_SHIM_MODEL", true);
    shimInput = readEnvvarBoolean("QTFB_SHIM_INPUT", true);
    shimFramebuffer = readEnvvarBoolean("QTFB_SHIM_FB", true);

    char *fbMode = getenv("QTFB_SHIM_MODE");
    if(fbMode != NULL) {
        if(strcmp(fbMode, "RM2FB") == 0) {
            shimType = FBFMT_RM2FB;
        } else if(strcmp(fbMode, "RGB888") == 0) {
            shimType = FBFMT_RMPP_RGB888;
        } else if(strcmp(fbMode, "RGBA8888") == 0) {
            shimType = FBFMT_RMPP_RGBA8888;
        } else if(strcmp(fbMode, "RGB565") == 0) {
            shimType = FBFMT_RMPP_RGB565;
        } else {
            fprintf(stderr, "No such mode supported: %s\n", fbMode);
            abort();
        }
    }

    char *fbKey = getenv("QTFB_SHIM_KEY");
    if(fbKey != NULL) {
        shimFramebufferKey = (unsigned int) strtoul(fbKey, NULL, 10);
    }
}

int spoofModelFD() {
    CERR << "Connected!" << std::endl;
    int modelSpoofFD = memfd_create("Spoof Model Number", 0);
    const char fakeModel[] = FAKE_MODEL;

    if(modelSpoofFD == -1) {
        CERR << "Failed to create memfd for model spoofing" << std::endl;
    }

    if(ftruncate(modelSpoofFD, sizeof(fakeModel) - 1) == -1) {
        CERR << "Failed to truncate memfd for model spoofing: " << errno << std::endl;
    }

    write(modelSpoofFD, fakeModel, sizeof(fakeModel) - 1);
    lseek(modelSpoofFD, 0, 0);
    return modelSpoofFD;
}

static int (*realOpen)(const char *, int, mode_t) = (int (*)(const char *, int, mode_t)) dlsym(RTLD_NEXT, "open");
inline int handleOpen(const char *fileName, int flags, mode_t mode) {
    if(shimModel)
        if(strcmp(fileName, FILE_MODEL) == 0 && shimModel) {
            return spoofModelFD();
        }

    int status;
    if(shimFramebuffer)
        if((status = fbShimOpen(fileName)) != INTERNAL_SHIM_NOT_APPLICABLE) {
            return status;
        }

    if(shimInput)
        if((status = inputShimOpen(fileName, realOpen, flags, mode)) != INTERNAL_SHIM_NOT_APPLICABLE) {
            return status;
        }

    return INTERNAL_SHIM_NOT_APPLICABLE;
}

extern "C" int close(int fd) {
    static int (*realClose)(int) = (int (*)(int)) dlsym(RTLD_NEXT, "close");

    int status;
    if(shimFramebuffer)
        if((status = fbShimClose(fd)) != INTERNAL_SHIM_NOT_APPLICABLE) {
            return status;
        }
    if(shimInput)
        if((status = inputShimClose(fd, realClose)) != INTERNAL_SHIM_NOT_APPLICABLE) {
            return status;
        }

    return realClose(fd);
}

extern "C" int ioctl(int fd, unsigned long request, char *ptr) {
    static int (*realIoctl)(int, unsigned long, ...) = (int (*)(int, unsigned long, ...)) dlsym(RTLD_NEXT, "ioctl");

    int status;
    if(shimFramebuffer)
        if((status = fbShimIoctl(fd, request, ptr)) != INTERNAL_SHIM_NOT_APPLICABLE) {
            return status;
        }
    if(shimInput)
        if((status = inputShimIoctl(fd, request, ptr, (int (*)(int, unsigned long, char *)) realIoctl)) != INTERNAL_SHIM_NOT_APPLICABLE) {
            return status;
        }

    return realIoctl(fd, request, ptr);
}

extern "C" int open64(const char *fileName, int flags, mode_t mode) {
    static int (*realOpen64)(const char *, int, mode_t) = (int (*)(const char *, int, mode_t)) dlsym(RTLD_NEXT, "open64");
    int fd;
    if((fd = handleOpen(fileName, flags, mode)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fd;
    }

    return realOpen64(fileName, flags, mode);
}

extern "C" int openat(int dirfd, const char *fileName, int flags, mode_t mode) {
    static int (*realOpenat)(int, const char *, int, mode_t) = (int (*)(int, const char *, int, mode_t)) dlsym(RTLD_NEXT, "openat");
    int fd;
    if((fd = handleOpen(fileName, flags, mode)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fd;
    }

    return realOpenat(dirfd, fileName, flags, mode);
}

extern "C" int open(const char *fileName, int flags, mode_t mode) {
    int fd;
    if((fd = handleOpen(fileName, flags, mode)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fd;
    }

    return realOpen(fileName, flags, mode);
}

extern "C" FILE *fopen(const char *fileName, const char *mode) {
    static FILE *(*realFopen)(const char *, const char *) = (FILE *(*)(const char *, const char *)) dlsym(RTLD_NEXT, "fopen");
    int fd;
    if((fd = handleOpen(fileName, 0, 0)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fdopen(fd, mode);
    }

    return realFopen(fileName, mode);
}

extern "C" FILE *fopen64(const char *fileName, const char *mode) {
    static FILE *(*realFopen)(const char *, const char *) = (FILE *(*)(const char *, const char *)) dlsym(RTLD_NEXT, "fopen64");
    int fd;
    if((fd = handleOpen(fileName, 0, 0)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fdopen(fd, mode);
    }

    return realFopen(fileName, mode);
}

extern "C" ssize_t read(int fd, void *buffer, size_t size) {
    static ssize_t (*realRead)(int, void *, size_t) = (ssize_t(*)(int, void *, size_t)) dlsym(RTLD_NEXT, "read");
    ssize_t status;
    if(shimInput)
        if((status = inputShimRead(fd, buffer, size, realRead)) != INTERNAL_SHIM_NOT_APPLICABLE) {
            return status;
        }
    return realRead(fd, buffer, size);
}
