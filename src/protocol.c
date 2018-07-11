
#include "protocol.h"

#include <sys/socket.h>
#include <sys/types.h>

ssize_t sendall(int socket_fd, void *packet, size_t size)
{
    uint8_t *buffer = (uint8_t *)packet;
    ssize_t sent_bytes = 0;
    while (size > 0) {
        sent_bytes = send(socket_fd, buffer, size, 0);
        if (sent_bytes <= 0) /* Lost Connection */
            return -1;
        buffer += sent_bytes;
        size -= sent_bytes;
    }
    return 0;
}

ssize_t recvall(int socket_fd, void *packet, size_t size)
{
    uint8_t *buffer = (uint8_t *)packet;
    ssize_t recvd_bytes = 0;
    while (size > 0) {
        recvd_bytes = recv(socket_fd, buffer, size, 0);
        if (recvd_bytes <= 0) /* Lost Connection */
            return -1;
        buffer += recvd_bytes;
        size -= recvd_bytes;
    }
    return 0;
}