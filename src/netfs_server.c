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

#define SERVER_ARGUMENT_COUNT 3

struct client_handler_args {
    int client_socket_fd;
};

void *client_handler(void *arg);

int server_sock_fd;
char *stor_dir;

void init(char *storage_dir, uint16_t port)
{
    stor_dir = storage_dir;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

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
        fprintf(stdout, "%s: Usage: %s [storage directory] [port]\n", argv[0],
                argv[0]);
        return EXIT_FAILURE;
    }
    init(argv[1], atoi(argv[2]));

    int client_sock_fd;
    struct client_handler_args *c_args;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(struct sockaddr_in);
    while (true) {
        /* Wait for connection. */
        if ((client_sock_fd =
                 accept(server_sock_fd, (struct sockaddr *)&client_addr,
                        &client_addr_size)) == -1) {
            fprintf(stderr, "Accepting client connection failed, Error: %s\n",
                    strerror(errno));
            continue;
        }
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
        if (recvall(client_socket_fd, &recv_packet_header, NETFS_HEADER_SIZE) <
            0) {
            fprintf(stdout, "Connection Lost \n"); //!!!
            break; // Connection loss.
        }
        recv_packet_header.payload_length =
            ntohl(recv_packet_header.payload_length);

        uint32_t send_payload_length;
        switch (recv_packet_header.operation) {
        case GETATTR: {
            char path[recv_packet_header.payload_length + 1];
            path[recv_packet_header.payload_length] = '\0';
            if (recvall(client_socket_fd, path,
                        recv_packet_header.payload_length) < 0)
                break;

            fprintf(stdout, "GETATTR: %s\n", path); //!!!

            char full_path[strlen(stor_dir) + strlen(path) + 1];
            strcpy(full_path, stor_dir);
            strcat(full_path, path);

            struct stat tmp_st;
            if (strstr(full_path, "..") !=
                    NULL || // Don't allow to leave stor_dir
                stat(full_path, &tmp_st) < 0) {
                /* Send errno */
                send_payload_length = sizeof(uint32_t);
                uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
                PREP_NETFS_HEADER(send_packet, send_payload_length, ERROR);
                *(uint32_t *)NETFS_PAYLOAD(send_packet) = htonl(errno);
                fprintf(stdout, "GETATTR: sending %s\n",
                        strerror(ntohl(
                            *(uint32_t *)NETFS_PAYLOAD(send_packet)))); //!!!
                if (sendall(client_socket_fd, send_packet,
                            NETFS_PACKET_SIZE(send_payload_length)) < 0)
                    break;
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

                if (sendall(client_socket_fd, send_packet,
                            NETFS_PACKET_SIZE(send_payload_length)) < 0)
                    break;
            }
        } break;
        case READDIR: {
            char path[recv_packet_header.payload_length + 1];
            path[recv_packet_header.payload_length] = '\0';
            if (recvall(client_socket_fd, path,
                        recv_packet_header.payload_length) < 0)
                break;

            char full_path[strlen(stor_dir) + strlen(path) + 1];
            strcpy(full_path, stor_dir);
            strcat(full_path, path);

            int dirf;
            struct stat dirstat;
            DIR *dirp;
            if (strstr(full_path, "..") != NULL ||
                (dirp = opendir(full_path)) == NULL ||
                (dirf = dirfd(dirp)) < 0 || fstat(dirf, &dirstat) < 0) {
                /* Send errno */
                send_payload_length = sizeof(uint32_t);
                uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
                PREP_NETFS_HEADER(send_packet, send_payload_length, ERROR);
                *(uint32_t *)NETFS_PAYLOAD(send_packet) = htonl(errno);
                fprintf(stdout, "READDIR: sending errno %d\n", errno); //!!!
                if (sendall(client_socket_fd, send_packet,
                            NETFS_PACKET_SIZE(send_payload_length)) < 0)
                    break;
            } else {
                fprintf(stdout, "READDIR %s\n", full_path); // !!!

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
                if (sendall(client_socket_fd, send_packet,
                            NETFS_PACKET_SIZE(send_payload_length)) < 0)
                    break;
                free(send_packet);
                closedir(dirp);
            }
        } break;
        case READ: {
            uint8_t recv_packet_payload[recv_packet_header.payload_length];
            if (recvall(client_socket_fd, recv_packet_payload,
                        recv_packet_header.payload_length) < 0)
                break;

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

            char full_path[strlen(stor_dir) + strlen(path) + 1];
            strcpy(full_path, stor_dir);
            strcat(full_path, path);

            void *send_packet = malloc(NETFS_PACKET_SIZE(inf->count));
            int fd, read_bytes;
            if (strstr(full_path, "..") != NULL ||
                (fd = open(full_path, O_RDONLY)) < 0 ||
                lseek(fd, inf->file_offset, SEEK_SET) < 0 ||
                (read_bytes =
                     read(fd, NETFS_PAYLOAD(send_packet), inf->count)) < 0) {
                /* Send errno */
                send_payload_length = sizeof(uint32_t);
                uint8_t send_packet[NETFS_PACKET_SIZE(send_payload_length)];
                PREP_NETFS_HEADER(send_packet, send_payload_length, ERROR);
                *(uint32_t *)NETFS_PAYLOAD(send_packet) = htonl(errno);
                fprintf(stdout, "READ: sending errno %d\n", errno); //!!!
                if (sendall(client_socket_fd, send_packet,
                            NETFS_PACKET_SIZE(send_payload_length)) < 0)
                    break;
            } else {
                send_payload_length = read_bytes;
                PREP_NETFS_HEADER(send_packet, send_payload_length, READ_R);
                if (sendall(client_socket_fd, send_packet,
                            NETFS_PACKET_SIZE(send_payload_length)) < 0)
                    break;
            }
            free(send_packet);
            close(fd);
        } break;
        default:
            fprintf(stderr, "Unknown packet: %u\n",
                    recv_packet_header.operation);
            free(arg);
            return NULL;
        }
    }
    free(arg);
    return NULL;
}