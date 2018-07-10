#define FUSE_USE_VERSION 26

#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

char *root_directory = "datadir/";
int err = 2; // stdout

static int netfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    char *full_path = malloc(strlen(path) + strlen(root_directory) + 1); // !!!
    strcpy(full_path, root_directory); // !!!
    strcat(full_path, path); // !!!

    struct stat tmp_st;
    if (stat(full_path, &tmp_st) == -1)
        return -errno;
    stbuf->st_mode = tmp_st.st_mode;
    stbuf->st_nlink = tmp_st.st_nlink;
    stbuf->st_uid = tmp_st.st_uid;
    stbuf->st_gid = tmp_st.st_gid;
    stbuf->st_size = tmp_st.st_size;

    stbuf->st_atime = tmp_st.st_atime;
    stbuf->st_mtime = tmp_st.st_mtime;
    stbuf->st_ctime = tmp_st.st_ctime;
    dprintf(err, "getattr %s %ld\n", full_path, pthread_self()); // !!!
    return 0;
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
    dprintf(err, "readdir %s %ld\n", path, pthread_self()); // !!!
    (void)offset;

    DIR *dirp = (DIR *)fi->fh;
    struct dirent *entry = readdir(dirp);
    while (1) {
        if (entry == NULL || filler(buf, entry->d_name, NULL, 0) != 0)
            break;
        entry = readdir(dirp);
    }

    return 0;
}

static int netfs_open(const char *path, struct fuse_file_info *fi)
{

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

/* -f Foreground, -s Single Threaded */
int main(int argc, char *argv[])
{
    struct fuse_operations netfs_oper = {
        .getattr = netfs_getattr,
        .opendir = netfs_opendir,
        .readdir = netfs_readdir,
        .open = netfs_open,
        .read = netfs_read,
    };
    fuse_main(argc, argv, &netfs_oper, NULL);

    return 0;
}