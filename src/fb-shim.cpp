#include <cstdio>
#include "fb-shim.h"
#include "shim.h"
#include "qtfb-client/qtfb-client.h"

// Ehhh..
#include <linux/fb.h>
#undef _IOW
#undef _IOWR
#define _IOWR _IOW
#define _IOW(_, nr, __) (nr | 0x40484600)
#include "mxcfb.h"

#define BYTES_PER_PIXEL 2
#define FILE_FB "/dev/fb0"

#define SHIM_WIDTH RMPP_WIDTH
#define SHIM_HEIGHT RMPP_HEIGHT

#ifdef _32BITFIXEDINFO
struct _32_bit_fb_fix_screeninfo {
    uint8_t id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t fb_type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint32_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};
#define remapped_fb_var_screeninfo _32_bit_fb_fix_screeninfo
#else
#define remapped_fb_var_screeninfo fb_fix_screeninfo
#endif

extern qtfb::FBKey shimFramebufferKey;
extern uint8_t shimType;

static qtfb::ClientConnection *clientConnection;
static void *shmMemory = NULL;
static int shmFD = -1;

int fbShimOpen(const char *file) {
    if(shmFD == -1 && strcmp(file, FILE_FB) == 0) {
        clientConnection = new qtfb::ClientConnection(shimFramebufferKey, shimType);
        shmFD = clientConnection->shmFd;
        shmMemory = clientConnection->shm;
    }
    return strcmp(file, FILE_FB) == 0 ? shmFD : INTERNAL_SHIM_NOT_APPLICABLE; 
}

int fbShimClose(int fd) {
    return fd == shmFD ? 0 : INTERNAL_SHIM_NOT_APPLICABLE;
}

int fbShimIoctl(int fd, unsigned long request, char *ptr) {
    if (fd == shmFD) {
        if (request == MXCFB_SEND_UPDATE) {
            mxcfb_update_data *update = (mxcfb_update_data *) ptr;
            clientConnection->sendPartialUpdate(
                update->update_region.left,
                update->update_region.top,
                update->update_region.width,
                update->update_region.height
            );
            return 0;
        } else if (request == MXCFB_SET_AUTO_UPDATE_MODE) {
            return 0;
        } else if (request == MXCFB_WAIT_FOR_UPDATE_COMPLETE) {
            // TODO ??
            return 0;
        }
        else if (request == FBIOGET_VSCREENINFO) {
            fb_var_screeninfo *screeninfo = (fb_var_screeninfo *)ptr;
            screeninfo->xres = SHIM_WIDTH;
            screeninfo->yres = SHIM_HEIGHT;
            screeninfo->grayscale = 0;
            screeninfo->bits_per_pixel = 8 * BYTES_PER_PIXEL;
            screeninfo->xres_virtual = SHIM_WIDTH;
            screeninfo->yres_virtual = SHIM_HEIGHT;

            screeninfo->red.offset = 11;
            screeninfo->red.length = 5;
            screeninfo->green.offset = 5;
            screeninfo->green.length = 6;
            screeninfo->blue.offset = 0;
            screeninfo->blue.length = 5;
            return 0;
        }
        else if (request == FBIOPUT_VSCREENINFO) {
            return 0;
        } else if (request == FBIOGET_FSCREENINFO) {
            remapped_fb_var_screeninfo *screeninfo = (remapped_fb_var_screeninfo *)ptr;
            screeninfo->smem_len = clientConnection->shmSize;
            screeninfo->smem_start = (unsigned long) shmMemory;
            screeninfo->line_length = SHIM_WIDTH * BYTES_PER_PIXEL;
            constexpr char fb_id[] = "mxcfb";
            memcpy(screeninfo->id, fb_id, sizeof(fb_id));
            return 0;
        } else {
            CERR << "UNHANDLED IOCTL" << ' ' << request << std::endl;
            return 0;
        }
    }
    return INTERNAL_SHIM_NOT_APPLICABLE;
}
