#include "qtfb-client.h"
#include <iostream>

qtfb::ClientConnection::ClientConnection(qtfb::FBKey framebufferID, uint8_t shmType){
    int sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(sock == -1) {
        std::cout << "Failed to initialize the socket!" << std::endl;
        exit(-1);
    }
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if(connect(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        std::cout << "Failed to connect. " << errno << std::endl;
        exit(-2);
    }

    // Ask to be connected to the main framebuffer.
    // Work in color (RMPP) mode
    qtfb::ClientMessage initMessage = {
        .type = MESSAGE_INITIALIZE,
        .init = {
            .framebufferKey = framebufferID,
            .framebufferType = shmType,
        },
    };
    if(send(sock, &initMessage, sizeof(initMessage), 0) == -1) {
        std::cout << "Failed to send init message!" << std::endl;
        exit(-3);
    }

    qtfb::ServerMessage incomingInitConfirm;
    if(recv(sock, &incomingInitConfirm, sizeof(incomingInitConfirm), 0) < 1) {
        std::cout << "Failed to recv init message!" << std::endl;
        exit(-4);
    }

    FORMAT_SHM(shmName, incomingInitConfirm.init.shmKeyDefined);

    int fd = shm_open(shmName, 02, 0);
    if(fd == -1) {
        std::cout << "Failed to get shm!" << std::endl;
        exit(-5);
    }
    shmFd = fd;

    unsigned char *memory = (unsigned char *) mmap(NULL, incomingInitConfirm.init.shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(memory == MAP_FAILED) {
        std::cout << "Failed to mmap() shm!" << std::endl;
        exit(-6);
    }
    this->fd = sock;
    shm = memory;
    shmSize = incomingInitConfirm.init.shmSize;
}

qtfb::ClientConnection::~ClientConnection() {
    munmap(shm, shmSize);
    close(fd);
}

void qtfb::ClientConnection::_send(const struct ClientMessage &msg) {
    send(fd, &msg, sizeof(msg), 0);
}

void qtfb::ClientConnection::sendCompleteUpdate(){
    _send({
            .type = MESSAGE_UPDATE,
            .update = {
                .type = UPDATE_ALL,
            },
        });
}

void qtfb::ClientConnection::sendPartialUpdate(int x, int y, int w, int h) {
    _send({
            .type = MESSAGE_UPDATE,
            .update = {
                .type = UPDATE_PARTIAL,
                .x = x, .y = y, .w = w, .h = h,
            },
        });
}
