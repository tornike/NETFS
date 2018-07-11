
#include <arpa/inet.h>
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"

#define SERVER_ARGUMENT_COUNT 4

#define FULL_PATH(path)                                                        \
    int full_path_size = strlen(stor_dir) + strlen(path);                      \
    char full_path[full_path_size + 1];                                        \
    strcpy(full_path, stor_dir);                                               \
    strcat(full_path, path)

struct client_handler_args {
    int client_socket_fd;
};

int server_sock_fd;
char *stor_dir;

void *client_handler(void *arg);

void init(char *ip, uint16_t port, char *storage_dir)
{
    stor_dir = storage_dir;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr.s_addr);

    if ((server_sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Socket creation failed! Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (bind(server_sock_fd, (struct sockaddr *)&addr,
             sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Server socket binding failed, Error: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (listen(server_sock_fd, 1)) {
        fprintf(stderr, "Server socket listen failed, Error: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    if (argc != SERVER_ARGUMENT_COUNT) {
        fprintf(stdout,
                "%s: Usage: %s [ipv4_address] [port] [storade directory]\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    init(argv[1], atoi(argv[2]), argv[3]);

    int client_sock_fd;
    struct client_handler_args *c_args;
    while (true) {
        /* Wait for connection. */
        struct sockaddr_in client_addr;
        socklen_t client_addr_size = sizeof(struct sockaddr_in);
        if ((client_sock_fd =
                 accept(server_sock_fd, (struct sockaddr *)&client_addr,
                        &client_addr_size)) == -1) {
            fprintf(stderr, "Accepting client connection Failed, Error: %s\n",
                    strerror(errno));
            continue;
        }
        fprintf(stdout, "Client connected\n"); //!!!
        c_args = malloc(sizeof(struct client_handler_args));
        c_args->client_socket_fd = client_sock_fd;
        pthread_t t;
        pthread_create(&t, NULL, client_handler, (void *)c_args);
    }

    return 0;
}

void *client_handler(void *arg)
{
    int client_socket_fd =
        ((struct client_handler_args *)arg)->client_socket_fd;

    struct netfs_header recv_packet_header;
    while (true) {
        if (recvall(client_socket_fd, &recv_packet_header, NETFS_HEADER_SIZE) ==
            -1)
            return NULL; // Connection loss.
        recv_packet_header.payload_length =
            ntohl(recv_packet_header.payload_length);

        uint32_t send_payload_length;
        switch (recv_packet_header.operation) {
        case GETATTR: {
            char file_path[recv_packet_header.payload_length + 1];
            file_path[recv_packet_header.payload_length] = '\0';
            recvall(client_socket_fd, file_path,
                    recv_packet_header.payload_length);

            fprintf(stdout, "GETATTR: %s\n", file_path); //!!!

            FULL_PATH(file_path);

            struct stat tmp_st;
            if (stat(full_path, &tmp_st) == -1) {
                /* send errno */
                uint32_t tmp_errno = errno;
                send_payload_length = sizeof(uint32_t);
                uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
                PREP_NETFS_HEADER(send_packet, send_payload_length, ERROR);
                *(uint32_t *)NETFS_PAYLOAD(send_packet) = htonl(tmp_errno);
                fprintf(stdout, "GETATTR: sending %s\n",
                        strerror(ntohl(
                            *(uint32_t *)NETFS_PAYLOAD(send_packet)))); //!!!
                sendall(client_socket_fd, send_packet,
                        NETFS_PACKET_SIZE(
                            send_payload_length)); // check return result
            } else {
                send_payload_length = sizeof(struct netfs_attrs);
                uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
                PREP_NETFS_HEADER(send_packet, send_payload_length, GETATTR_R);

                struct netfs_attrs *send_payload =
                    (struct netfs_attrs *)NETFS_PAYLOAD(send_packet);
                send_payload->mode = htonl(tmp_st.st_mode);
                send_payload->nlink = htonl(tmp_st.st_nlink);
                send_payload->uid = htonl(tmp_st.st_uid);
                send_payload->gid = htonl(tmp_st.st_gid);
                send_payload->size = htonl(tmp_st.st_size);
                send_payload->atime = htonl(tmp_st.st_atime);
                send_payload->mtime = htonl(tmp_st.st_mtime);
                send_payload->ctime = htonl(tmp_st.st_ctime);

                sendall(client_socket_fd, send_packet,
                        NETFS_PACKET_SIZE(
                            send_payload_length)); // check return result
            }
        } break;
        case READDIR: {
            char file_path[recv_packet_header.payload_length + 1];
            file_path[recv_packet_header.payload_length] = '\0';
            recvall(client_socket_fd, file_path,
                    recv_packet_header.payload_length);

            FULL_PATH(file_path);

            DIR *dirp = opendir(full_path);
            if (dirp == NULL) {
                /* send errno */
                uint32_t tmp_errno = errno;
                send_payload_length = sizeof(uint32_t);
                uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
                PREP_NETFS_HEADER(send_packet, send_payload_length, ERROR);
                *(uint32_t *)NETFS_PAYLOAD(send_packet) = htonl(tmp_errno);
                fprintf(stdout, "READDIR: sending errno %d\n", tmp_errno); //!!!
                sendall(client_socket_fd, send_packet,
                        NETFS_PACKET_SIZE(
                            send_payload_length)); // check return result
            } else {
                fprintf(stdout, "READDIR %s\n", full_path); // !!!
                int dirf = dirfd(dirp);
                struct stat dirstat;
                fstat(dirf, &dirstat);

                void *send_packet = malloc(NETFS_PACKET_SIZE(dirstat.st_size));
                char *send_payload = (char *)NETFS_PAYLOAD(send_packet);

                send_payload_length = 0;
                struct dirent *entry;
                while (true) {
                    entry = readdir(dirp);
                    if (entry == NULL)
                        break;
                    uint8_t str_length = strlen(entry->d_name);
                    send_payload[send_payload_length++] = str_length;
                    strncpy(OFFSET(send_payload, send_payload_length),
                            entry->d_name, str_length);
                    send_payload_length += str_length;
                }
                PREP_NETFS_HEADER(send_packet, send_payload_length, READDIR_R);
                sendall(client_socket_fd, send_packet,
                        NETFS_PACKET_SIZE(send_payload_length));
                free(send_packet);
                closedir(dirp);
            }
        } break;
        case READ: {
            uint8_t recv_packet_payload[recv_packet_header.payload_length];
            recvall(client_socket_fd, recv_packet_payload,
                    recv_packet_header.payload_length);

            struct netfs_read_write *inf =
                (struct netfs_read_write *)recv_packet_payload;
            inf->path_len = ntohl(inf->path_len);
            inf->count = be64toh(inf->count);
            inf->file_offset = be64toh(inf->file_offset);

            char path[inf->path_len + 1];
            path[inf->path_len] = '\0';
            strncpy(
                path,
                OFFSET(recv_packet_payload, sizeof(struct netfs_read_write)),
                inf->path_len);
            fprintf(stdout, "READ %s %lu %lu\n", path, inf->count,
                    inf->file_offset); // !!!

            FULL_PATH(path);

            int fd = open(full_path, O_RDWR);
            if (fd == -1 || lseek(fd, inf->file_offset, SEEK_SET) == -1) {
                /* Send errno */
                uint32_t tmp_errno = errno;
                send_payload_length = sizeof(uint32_t);
                uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
                PREP_NETFS_HEADER(send_packet, send_payload_length, ERROR);
                *(uint32_t *)NETFS_PAYLOAD(send_packet) = htonl(tmp_errno);
                fprintf(stdout, "READ: sending errno %d\n", tmp_errno); //!!!
                sendall(client_socket_fd, send_packet,
                        NETFS_PACKET_SIZE(
                            send_payload_length)); // check return result
            } else {
                void *send_packet = malloc(NETFS_PACKET_SIZE(inf->count));

                int read_bytes =
                    read(fd, NETFS_PAYLOAD(send_packet), inf->count);
                if (read_bytes == -1) {
                    /* Send errno */
                    uint32_t tmp_errno = errno;
                    send_payload_length = sizeof(uint32_t);
                    PREP_NETFS_HEADER(send_packet, send_payload_length, ERROR);
                    *(uint32_t *)NETFS_PAYLOAD(send_packet) = htonl(tmp_errno);
                    fprintf(stdout, "READ: Sending errno %d\n",
                            tmp_errno); //!!!
                } else {
                    send_payload_length = read_bytes;
                    PREP_NETFS_HEADER(send_packet, send_payload_length, READ_R);
                }
                sendall(client_socket_fd, send_packet,
                        NETFS_PACKET_SIZE(
                            send_payload_length)); // check return result
                free(send_packet);
            }
            close(fd);
        } break;
        default:
            fprintf(stdout, "UNKNOWN OP %u\n", recv_packet_header.operation);
            return NULL;
        }
    }
    return NULL;
}