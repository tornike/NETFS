
#define FUSE_USE_VERSION 26

#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"

#define CLIENT_ARGUMENT_COUNT 4

struct netfs_config {
    struct sockaddr_in server_addr;
    int sock_fd;
};

/* Fuse override function prototypes. */
static int netfs_getattr(const char *path, struct stat *stbuf);
static int netfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi);
static int netfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi);

struct netfs_config cfg;

void init(char *ip, uint16_t port)
{
    memset(&cfg, 0, sizeof(struct netfs_config));
    cfg.server_addr.sin_family = AF_INET;
    cfg.server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &cfg.server_addr.sin_addr.s_addr);

    if ((cfg.sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        dprintf(STDOUT_FILENO, "Socket creation failed! Error: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Multiple ?
    if (connect(cfg.sock_fd, (struct sockaddr *)&cfg.server_addr,
                sizeof(struct sockaddr_in)) < 0) {
        printf("Could not connect %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/* -f Foreground, -s Single Threaded */
int main(int argc, char *argv[])
{
    if (argc < CLIENT_ARGUMENT_COUNT) {
        fprintf(stdout,
                "%s: Usage: %s [optional: arguments for fuse] [mount point] "
                "[storage address] [storage port]\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    init(argv[argc - 2], atoi(argv[argc - 1]));
    struct fuse_operations netfs_oper = {
        .getattr = netfs_getattr,
        .readdir = netfs_readdir,
        .read = netfs_read,
    };
    fuse_main(argc - 2, argv, &netfs_oper, NULL);

    return 0;
}

static int netfs_getattr(const char *path, struct stat *stbuf)
{
    fprintf(stderr, "Getattr %s %ld\n", path, pthread_self()); // !!!

    memset(stbuf, 0, sizeof(struct stat));

    uint32_t send_payload_length = strlen(path);
    uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
    PREP_NETFS_HEADER(send_packet, send_payload_length, GETATTR);

    strncpy(NETFS_PAYLOAD(send_packet), path, send_payload_length);

    struct netfs_header recv_packet_header;
    if (sendall(cfg.sock_fd, send_packet,
                NETFS_PACKET_SIZE(send_payload_length)) < 0) {
        printf("Connection Lost Send %s\n", strerror(errno));
        return -ENOENT;
    }
    if (recvall(cfg.sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) < 0) {
        printf("Connection Lost Recv %s\n", strerror(errno));
        return -ENOENT;
    }

    if (recv_packet_header.operation != GETATTR_R &&
        recv_packet_header.operation != ERROR) {
        fprintf(stderr, "Unknown packet in GETATTR %d\n",
                recv_packet_header.operation);
    }

    recv_packet_header.payload_length =
        ntohl(recv_packet_header.payload_length);
    uint8_t recv_packet_payload[recv_packet_header.payload_length];
    if (recvall(cfg.sock_fd, &recv_packet_payload,
                recv_packet_header.payload_length) < 0) {
        printf("Connection Lost Recv 2 %s\n", strerror(errno));
        return -ENOENT;
    }
    if (recv_packet_header.operation == ERROR) {
        errno = ntohl(*(uint32_t *)recv_packet_payload);
        printf("GETATTR %s ERROR %d\n", path, errno); // !!!
        return -errno;
    } else {
        struct netfs_attrs *attrs = (struct netfs_attrs *)recv_packet_payload;
        stbuf->st_mode = ntohl(attrs->mode);
        stbuf->st_nlink = ntohl(attrs->nlink);
        stbuf->st_uid = ntohl(attrs->uid);
        stbuf->st_gid = ntohl(attrs->gid);
        stbuf->st_size = ntohl(attrs->size);
        stbuf->st_atime = ntohl(attrs->atime);
        stbuf->st_mtime = ntohl(attrs->mtime);
        stbuf->st_ctime = ntohl(attrs->ctime);
        return 0;
    }
}

static int netfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    fprintf(stdout, "readdir %s %ld\n", path, pthread_self()); // !!!
    (void)offset;

    uint32_t send_payload_length = strlen(path);
    uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
    PREP_NETFS_HEADER(send_packet, send_payload_length, READDIR);
    strncpy(NETFS_PAYLOAD(send_packet), path, send_payload_length);

    struct netfs_header recv_packet_header;
    if (sendall(cfg.sock_fd, send_packet,
                NETFS_PACKET_SIZE(send_payload_length)) < 0) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    }
    if (recvall(cfg.sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) < 0) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    }

    if (recv_packet_header.operation != READDIR_R &&
        recv_packet_header.operation != ERROR) {
        fprintf(stderr, "Unknown packet in READDIR %d\n",
                recv_packet_header.operation);
    }

    recv_packet_header.payload_length =
        ntohl(recv_packet_header.payload_length);
    uint8_t recv_packet_payload[recv_packet_header.payload_length];
    if (recvall(cfg.sock_fd, recv_packet_payload,
                recv_packet_header.payload_length) < 0) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    }
    if (recv_packet_header.operation == ERROR) {
        errno = ntohl(*(uint32_t *)recv_packet_payload);
        printf("READDIR0 %s ERROR %d\n", path, errno); // !!!
        return -errno;
    } else {
        char *d_names = (char *)recv_packet_payload;
        uint8_t dname_len = 0;
        int i = 0;
        for (; i < recv_packet_header.payload_length; i += dname_len + 1) {
            dname_len = d_names[i];
            char dname[dname_len + 1];
            dname[dname_len] = '\0';
            strncpy(dname, &d_names[i] + 1, dname_len);
            filler(buf, (const char *)dname, NULL, 0);
        }
        printf("READDIR0 %s %u\n", path,
               recv_packet_header.payload_length); // !!!
        return 0;
    }
    return -ENOENT;
}

static int netfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    fprintf(stdout, "Read %s %ld\n", path, pthread_self()); // !!!

    int path_len = strlen(path);
    uint32_t send_payload_length = sizeof(struct netfs_read_write) + path_len;
    uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
    PREP_NETFS_HEADER(send_packet, send_payload_length, READ);

    struct netfs_read_write *send_payload =
        (struct netfs_read_write *)NETFS_PAYLOAD(send_packet);
    send_payload->path_len = htonl(path_len);
    send_payload->count = htobe64(size);
    send_payload->file_offset = htobe64(offset);
    strncpy(OFFSET(send_payload, sizeof(struct netfs_read_write)), path,
            path_len);

    struct netfs_header recv_packet_header;
    if (sendall(cfg.sock_fd, send_packet,
                NETFS_PACKET_SIZE(send_payload_length)) < 0) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    }
    if (recvall(cfg.sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) < 0) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    }

    if (recv_packet_header.operation != READ_R &&
        recv_packet_header.operation != ERROR) {
        fprintf(stderr, "Unknown packet in READ %d\n",
                recv_packet_header.operation);
    }

    recv_packet_header.payload_length =
        ntohl(recv_packet_header.payload_length);
    void *recv_packet_payload = malloc(recv_packet_header.payload_length);
    if (recvall(cfg.sock_fd, recv_packet_payload,
                recv_packet_header.payload_length) < 0) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        free(recv_packet_payload);
        return -ENOENT;
    }

    if (recv_packet_header.operation == ERROR) {
        errno = ntohl(*(uint32_t *)recv_packet_payload);
        printf("READ ERROR%d\n", errno);
        free(recv_packet_payload);
        return -errno;
    } else {
        printf("READ: %s %lu %lu\n", path, size, offset); // !!!
        int read_bytes = recv_packet_header.payload_length;
        memcpy(buf, recv_packet_payload, recv_packet_header.payload_length);
        free(recv_packet_payload);
        return read_bytes;
    }

    return -ENOENT;
}
