#pragma once
#include <sys/types.h>

int inputShimOpen(const char *file, int (*realOpen)(const char *name, int flags, mode_t mode), int flags, mode_t mode);
int inputShimClose(int fd, int (*realClose)(int fd));
ssize_t inputShimRead(int fd, void *bfr, unsigned long size, ssize_t (*original)(int fd, void *bfr, size_t size));
int inputShimIoctl(int fd, unsigned long request, char *ptr, int (*realFopen)(int fd, unsigned long request, char *ptr));
