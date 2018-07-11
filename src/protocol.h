#ifndef __NET_FS_PROTOCOL__
#define __NET_FS_PROTOCOL__

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint8_t netfs_oper;

struct netfs_header {
    uint32_t payload_length;
    netfs_oper operation;
} __attribute__((packed));

struct netfs_attrs {
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
} __attribute__((packed));

struct netfs_read_write {
    uint32_t path_len;
    uint64_t file_offset;
    uint64_t count;
} __attribute__((packed));

/* Operation Types */
#define GETATTR 1
#define GETATTR_R 2 // Response
#define READDIR 3
#define READDIR_R 4
#define READ 7
#define READ_R 8
#define ERROR 9

/* Useful macros */
#define OFFSET(pointer, off) ((char *)pointer + off)
#define NETFS_HEADER_SIZE sizeof(struct netfs_header)
#define NETFS_PACKET_SIZE(payload_length) (NETFS_HEADER_SIZE + payload_length)
#define NETFS_PAYLOAD(packet_ptr) OFFSET(packet_ptr, NETFS_HEADER_SIZE)
#define PREP_NETFS_HEADER(packet, payload_size, op)                            \
    ((struct netfs_header *)packet)->payload_length = htonl(payload_size);     \
    ((struct netfs_header *)packet)->operation = op

/* Helper Functions */
ssize_t sendall(int socket_fd, void *packet, size_t size);
ssize_t recvall(int socket_fd, void *packet, size_t size);

#endif