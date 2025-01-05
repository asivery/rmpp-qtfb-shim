#pragma once
#include <stdint.h>
#include <sys/types.h>
#define QTFB_DEFAULT_FRAMEBUFFER 245209899
#define SOCKET_PATH "/tmp/qtfb.sock"
#define FORMAT_SHM(var, key) char var[20]; snprintf(var, 20, "/qtfb_%d", key)

#define RM2_WIDTH 1404
#define RM2_HEIGHT 1872
#define RMPP_WIDTH 1620
#define RMPP_HEIGHT 2160

#define MESSAGE_INITIALIZE 0
#define MESSAGE_UPDATE 1

#define FBFMT_RM2FB 0
#define FBFMT_RMPP_RGB888 1
#define FBFMT_RMPP_RGBA8888 2
#define FBFMT_RMPP_RGB565 3

#define UPDATE_ALL 0
#define UPDATE_PARTIAL 1

namespace qtfb {

    typedef unsigned int FBKey;

    struct InitMessageContents {
        FBKey framebufferKey;
        uint8_t framebufferType;
    };

    struct InitMessageResponseContents {
        int shmKeyDefined;
        size_t shmSize;
    };

    struct UpdateRegionMessageContents {
        int type;
        int x, y, w, h;
    };

    struct ClientMessage {
        uint8_t type;
        union {
            struct InitMessageContents init;
            struct UpdateRegionMessageContents update;
        };
    };

    struct ServerMessage {
        uint8_t type;
        struct InitMessageResponseContents init;
    };
}
