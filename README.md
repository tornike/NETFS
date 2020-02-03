# Net FS

NETFS is a simple program for linux, which can mount remote host filesystem subtree to local machine, using fuse.

NETFS is a server client program. netfs_server runs on remote host while
netfs_client connects to it and mounts itself to mountpoint provided by 
user.

In order to build NETFS following packages should be installed:
pkg-config libfuse-dev