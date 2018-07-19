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
#include "utlist.h"

#define CLIENT_ARGUMENT_COUNT 4
#define MAX_CONNECTIONS 4

struct netfs_connection {
    int sock_fd;

    struct netfs_connection *next;
    struct netfs_connection *prev;
};

struct netfs_config {
    struct sockaddr_in server_addr;

    struct netfs_connection *connections;
    int connection_count;
    pthread_mutex_t connections_lock;
    pthread_cond_t connections_cond;
};

/* Function prototypes. */
struct netfs_connection *create_connection();
void remove_connection(struct netfs_connection *);
struct netfs_connection *get_connection();
void add_connection(struct netfs_connection *);

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

    cfg.connections = NULL;
    pthread_mutex_init(&cfg.connections_lock, NULL);
    pthread_cond_init(&cfg.connections_cond, NULL);
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

struct netfs_connection *create_connection()
{
    struct netfs_connection *new_con = malloc(sizeof(struct netfs_connection));

    if ((new_con->sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Socket creation failed! Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (connect(new_con->sock_fd, (struct sockaddr *)&cfg.server_addr,
                sizeof(struct sockaddr_in)) < 0) {
        fprintf(stderr, "Could not connect %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    cfg.connection_count++;
    return new_con;
}

void remove_connection(struct netfs_connection *con)
{
    pthread_mutex_lock(&cfg.connections_lock);
    close(con->sock_fd);
    free(con);
    cfg.connection_count--;
    pthread_mutex_unlock(&cfg.connections_lock);
}

struct netfs_connection *get_connection()
{
    struct netfs_connection *con = NULL;
    pthread_mutex_lock(&cfg.connections_lock);
    if (cfg.connections == NULL) {
        if (cfg.connection_count < MAX_CONNECTIONS) {
            con = create_connection();
        } else {
            pthread_cond_wait(&cfg.connections_cond, &cfg.connections_lock);
            con = cfg.connections;
            DL_DELETE(cfg.connections, con);
        }
    } else {
        con = cfg.connections;
        DL_DELETE(cfg.connections, con);
    }
    pthread_mutex_unlock(&cfg.connections_lock);
    return con;
}

void add_connection(struct netfs_connection *con)
{
    pthread_mutex_lock(&cfg.connections_lock);
    DL_APPEND(cfg.connections, con);
    pthread_cond_signal(&cfg.connections_cond);
    pthread_mutex_unlock(&cfg.connections_lock);
}

static int netfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    uint32_t send_payload_length = strlen(path);
    uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
    PREP_NETFS_HEADER(send_packet, send_payload_length, GETATTR);

    strncpy(NETFS_PAYLOAD(send_packet), path, send_payload_length);

    struct netfs_connection *con = get_connection();
    struct netfs_header recv_packet_header;
    if (sendall(con->sock_fd, send_packet,
                NETFS_PACKET_SIZE(send_payload_length)) < 0) {
        printf("Connection Lost %s\n", strerror(errno));
        remove_connection(con);
        return -ENOENT;
    }
    if (recvall(con->sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) < 0) {
        printf("Connection Lost %s\n", strerror(errno));
        remove_connection(con);
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
    if (recvall(con->sock_fd, &recv_packet_payload,
                recv_packet_header.payload_length) < 0) {
        printf("Connection Lost %s\n", strerror(errno));
        remove_connection(con);
        return -ENOENT;
    }
    if (recv_packet_header.operation == ERROR) {
        errno = ntohl(*(uint32_t *)recv_packet_payload);
        add_connection(con);
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
        add_connection(con);
        return 0;
    }
}

static int netfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    uint32_t send_payload_length = strlen(path);
    uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
    PREP_NETFS_HEADER(send_packet, send_payload_length, READDIR);
    strncpy(NETFS_PAYLOAD(send_packet), path, send_payload_length);

    struct netfs_connection *con = get_connection();
    struct netfs_header recv_packet_header;
    if (sendall(con->sock_fd, send_packet,
                NETFS_PACKET_SIZE(send_payload_length)) < 0) {
        fprintf(stderr, "Connection Lost %s\n", strerror(errno));
        remove_connection(con);
        return -ENOENT;
    }
    if (recvall(con->sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) < 0) {
        fprintf(stderr, "Connection Lost %s\n", strerror(errno));
        remove_connection(con);
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
    if (recvall(con->sock_fd, recv_packet_payload,
                recv_packet_header.payload_length) < 0) {
        fprintf(stderr, "Connection Lost %s\n", strerror(errno));
        remove_connection(con);
        return -ENOENT;
    }
    if (recv_packet_header.operation == ERROR) {
        errno = ntohl(*(uint32_t *)recv_packet_payload);
        add_connection(con);
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
        add_connection(con);
        return 0;
    }
    add_connection(con);
    return -ENOENT;
}

static int netfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
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

    struct netfs_connection *con = get_connection();
    struct netfs_header recv_packet_header;
    if (sendall(con->sock_fd, send_packet,
                NETFS_PACKET_SIZE(send_payload_length)) < 0) {
        fprintf(stderr, "Connection Lost %s\n", strerror(errno));
        remove_connection(con);
        return -ENOENT;
    }
    if (recvall(con->sock_fd, &recv_packet_header, NETFS_HEADER_SIZE) < 0) {
        fprintf(stderr, "Connection Lost %s\n", strerror(errno));
        remove_connection(con);
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
    if (recvall(con->sock_fd, recv_packet_payload,
                recv_packet_header.payload_length) < 0) {
        fprintf(stderr, "Connection Lost %s\n", strerror(errno));
        free(recv_packet_payload);
        remove_connection(con);
        return -ENOENT;
    }

    if (recv_packet_header.operation == ERROR) {
        errno = ntohl(*(uint32_t *)recv_packet_payload);
        free(recv_packet_payload);
        add_connection(con);
        return -errno;
    } else {
        int read_bytes = recv_packet_header.payload_length;
        memcpy(buf, recv_packet_payload, recv_packet_header.payload_length);
        free(recv_packet_payload);
        add_connection(con);
        return read_bytes;
    }

    add_connection(con);
    return -ENOENT;
}
