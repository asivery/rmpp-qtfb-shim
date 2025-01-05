#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include "shim.h"
#include "fb-shim.h"
#include "input-shim.h"
#include <sys/mman.h>

#define FAKE_MODEL "reMarkable 1.0"
#define FILE_MODEL "/sys/devices/soc0/machine"

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

static int (*realOpen)(const char *, int, int) = (int (*)(const char *, int, int)) dlsym(RTLD_NEXT, "open");
inline int handleOpen(const char *fileName) {
    if(strcmp(fileName, FILE_MODEL) == 0) {
        return spoofModelFD();
    }

    int status;
    if((status = fbShimOpen(fileName)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return status;
    }

    if((status = inputShimOpen(fileName, realOpen)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return status;
    }

    return INTERNAL_SHIM_NOT_APPLICABLE;
}

extern "C" int close(int fd) {
    static int (*realClose)(int) = (int (*)(int)) dlsym(RTLD_NEXT, "close");

    int status;
    if((status = fbShimClose(fd)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return status;
    }
    if((status = inputShimClose(fd, realClose)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return status;
    }

    return realClose(fd);
}

extern "C" int ioctl(int fd, unsigned long request, char *ptr) {
    static int (*realIoctl)(int, unsigned long, ...) = (int (*)(int, unsigned long, ...)) dlsym(RTLD_NEXT, "ioctl");

    int status;
    if((status = fbShimIoctl(fd, request, ptr)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return status;
    }
    if((status = inputShimIoctl(fd, request, ptr, (int (*)(int, unsigned long, char *)) realIoctl)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return status;
    }

    return realIoctl(fd, request, ptr);
}

extern "C" int open64(const char *fileName, int flags, mode_t mode) {
    static int (*realOpen64)(const char *, int, mode_t) = (int (*)(const char *, int, mode_t)) dlsym(RTLD_NEXT, "open64");
    int fd;
    if((fd = handleOpen(fileName)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fd;
    }

    return realOpen64(fileName, flags, mode);
}

extern "C" int openat(int dirfd, const char *fileName, int flags, mode_t mode) {
    static int (*realOpenat)(int, const char *, int, mode_t) = (int (*)(int, const char *, int, mode_t)) dlsym(RTLD_NEXT, "openat");
    int fd;
    if((fd = handleOpen(fileName)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fd;
    }

    return realOpenat(dirfd, fileName, flags, mode);
}

extern "C" int open(const char *fileName, int flags, mode_t mode) {
    int fd;
    if((fd = handleOpen(fileName)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fd;
    }

    return realOpen(fileName, flags, mode);
}

extern "C" FILE *fopen(const char *fileName, const char *mode) {
    static FILE *(*realFopen)(const char *, const char *) = (FILE *(*)(const char *, const char *)) dlsym(RTLD_NEXT, "fopen");
    int fd;
    if((fd = handleOpen(fileName)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fdopen(fd, mode);
    }

    return realFopen(fileName, mode);
}

extern "C" FILE *fopen64(const char *fileName, const char *mode) {
    static FILE *(*realFopen)(const char *, const char *) = (FILE *(*)(const char *, const char *)) dlsym(RTLD_NEXT, "fopen64");
    int fd;
    if((fd = handleOpen(fileName)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return fdopen(fd, mode);
    }

    return realFopen(fileName, mode);
}

extern "C" ssize_t read(int fd, void *buffer, size_t size) {
    static ssize_t (*realRead)(int, void *, size_t) = (ssize_t(*)(int, void *, size_t)) dlsym(RTLD_NEXT, "read");
    ssize_t status;
    if((status = inputShimRead(fd, buffer, size, realRead)) != INTERNAL_SHIM_NOT_APPLICABLE) {
        return status;
    }
    return realRead(fd, buffer, size);
}
