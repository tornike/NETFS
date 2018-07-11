
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

char *root_directory = "datadir/";
int err = 2; // stdout

struct netfs_config {
    char server_name[20];
    struct sockaddr_in server_addr;
    int sock_fd;
};

/* Fuse override function prototypes. */
static int netfs_getattr(const char *path, struct stat *stbuf);
static int netfs_opendir(const char *path, struct fuse_file_info *fi);
static int netfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi);
static int netfs_open(const char *path, struct fuse_file_info *fi);
static int netfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi);

struct netfs_config cfg;

void init(char *ip, uint16_t port)
{
    memset(&cfg, 0, sizeof(struct netfs_config));
    cfg.server_addr.sin_family = AF_INET;
    cfg.server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &cfg.server_addr.sin_addr.s_addr);

    if ((cfg.sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        dprintf(STDOUT_FILENO, "Socket creation failed! Error: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Multiple ?
    if (connect(cfg.sock_fd, (struct sockaddr *)&cfg.server_addr,
                sizeof(struct sockaddr_in)) != 0) {
        printf("Could not connect %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/* -f Foreground, -s Single Threaded */
int main(int argc, char *argv[])
{
    init("127.0.0.1", 34344);
    struct fuse_operations netfs_oper = {
        .getattr = netfs_getattr,
        //.opendir = netfs_opendir,
        .readdir = netfs_readdir,
        //.open = netfs_open,
        //.read = netfs_read,
    };
    fuse_main(argc, argv, &netfs_oper, NULL);

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
                NETFS_PACKET_SIZE(send_payload_length)) == -1) {
        printf("Connection Lost Send %s\n", strerror(errno));
        return -ENOENT;
    }
    if (recvall(cfg.sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) == -1) {
        printf("Connection Lost Recv %s\n", strerror(errno));
        return -ENOENT;
    }

    //
    if (recv_packet_header.operation != GETATTR_R &&
        recv_packet_header.operation != ERROR) {
        printf("Wrong Packet GETATTR %d\n", recv_packet_header.operation);
    }
    //

    recv_packet_header.payload_length =
        ntohl(recv_packet_header.payload_length);
    uint8_t recv_packet_payload[recv_packet_header.payload_length];
    if (recvall(cfg.sock_fd, &recv_packet_payload,
                recv_packet_header.payload_length) == -1) {
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

static int netfs_opendir(const char *path, struct fuse_file_info *fi)
{
    dprintf(err, "opendir %s %ld\n", path, pthread_self()); // !!!

    char *full_path = malloc(strlen(path) + strlen(root_directory) + 1); // !!!
    strcpy(full_path, root_directory); // !!!
    strcat(full_path, path); // !!!

    DIR *dirp = opendir(full_path);
    if (dirp == NULL)
        return -errno;
    fi->fh = (uint64_t)dirp;
    return 0;
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
                NETFS_PACKET_SIZE(send_payload_length)) == -1) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    }
    if (recvall(cfg.sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) == -1) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    }

    //
    if (recv_packet_header.operation != READDIR_R &&
        recv_packet_header.operation != ERROR) {
        printf("Wrong Packet READDIR\n");
    }
    //

    recv_packet_header.payload_length =
        ntohl(recv_packet_header.payload_length);
    uint8_t recv_packet_payload[recv_packet_header.payload_length];
    if (recvall(cfg.sock_fd, recv_packet_payload,
                recv_packet_header.payload_length) == -1) {
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

static int netfs_open(const char *path, struct fuse_file_info *fi)
{
    int path_len = strlen(path);
    uint32_t send_payload_length = sizeof(int) + path_len;
    uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
    PREP_NETFS_HEADER(send_packet, send_payload_length, OPEN);
    void *send_payload = NETFS_PAYLOAD(send_packet);
    *(int *)send_payload = htonl(fi->flags);
    strncpy(OFFSET(send_payload, sizeof(int)), path, path_len);

    struct netfs_header recv_packet_header;
    if (sendall(cfg.sock_fd, send_packet,
                NETFS_PACKET_SIZE(send_payload_length)) == -1) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    } else if (recvall(cfg.sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) ==
               -1) {
        fprintf(stderr, "No connection %s\n", strerror(errno));
        return -ENOENT;
    } else {
        assert(recv_packet_header.operation == OPEN ||
               recv_packet_header.operation == ERROR);
        recv_packet_header.payload_length =
            ntohl(recv_packet_header.payload_length);
        // if (recvall(cfg.sock_fd, hash0,
        // recv_packet_header.payload_length) ==
        //     -1) {
        //     RECONNECT(0);
        // } else {
        //     if (recv_packet_header.operation == ERROR) {
        //         errno = ntohl(*(uint32_t *)hash0);
        //         printf("OPEN0 ERROR%d\n", errno);
        //         pthread_mutex_unlock(&config->lock);
        //         return -errno;
        //     } else {
        //         printf("OPEN0: %s\n", path); // !!!
        //     }
        // }
    }

    char *full_path = malloc(strlen(path) + strlen(root_directory) + 1); // !!!
    strcpy(full_path, root_directory); // !!!
    strcat(full_path, path); // !!!

    int fd = open(full_path, fi->flags);
    if (fd == -1)
        return -errno;
    fi->fh = fd;

    dprintf(err, "open %s %u %ld\n", path, fi, pthread_self()); // !!!
    return 0;
}

static int netfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    if (lseek(fi->fh, offset, SEEK_SET) == -1)
        return -errno;
    int rd = read(fi->fh, buf, size);
    if (rd == -1)
        return -errno;

    dprintf(err, "read %s %lu %lu %ld\n", path, size, offset,
            pthread_self()); // !!!
    return rd;
}
