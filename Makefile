CC = gcc
CFLAGS_RELEASE = -Wall -c
CFLAGS_DEBUG = -Wall -g -c
LIBS = -lpthread
HEADERS = protocol.h

OBJECT_DIR = build/
SRC_DIR = src/

all: check $(OBJECT_DIR)netfs_client $(OBJECT_DIR)netfs_server

check:
ifeq ("$(wildcard $(OBJECT_DIR))", "")
	mkdir $(OBJECT_DIR)
endif

$(OBJECT_DIR)netfs_client: $(OBJECT_DIR)netfs_client.o $(OBJECT_DIR)protocol.o
	$(CC) $^ -o $@ `pkg-config fuse --cflags --libs` $(LIBS)

$(OBJECT_DIR)netfs_server: $(OBJECT_DIR)netfs_server.o $(OBJECT_DIR)protocol.o
	$(CC) $^ -o $@ $(LIBS)

$(OBJECT_DIR)netfs_client.o: $(SRC_DIR)netfs_client.c
	$(CC) `pkg-config fuse --cflags --libs` $(CFLAGS_DEBUG) $< -o $@

$(OBJECT_DIR)netfs_server.o: $(SRC_DIR)netfs_server.c
	$(CC) $(CFLAGS_DEBUG) $< -o $@

$(OBJECT_DIR)protocol.o: $(SRC_DIR)protocol.c
	$(CC) $(CFLAGS_DEBUG) $< -o $@

clean:
	rm $(OBJECT_DIR)*

test:
	if [ ! -d $(OBJECT_DIR) ]; then mkdir OBJECT_DIR; fi

	