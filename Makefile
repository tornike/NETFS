CC = gcc
CFLAGS_RELEASE = -Wall `pkg-config fuse --cflags --libs` -c
CFLAGS_DEBUG = -Wall `pkg-config fuse --cflags --libs` -g -c
LIBS = `pkg-config fuse --cflags --libs` -lpthread
#HEADERS = 

OBJECT_DIR = build/
SRC_DIR = src/

all: check $(OBJECT_DIR)netfs_client

check:
ifeq ("$(wildcard $(OBJECT_DIR))", "")
	mkdir $(OBJECT_DIR)
endif

$(OBJECT_DIR)netfs_client: $(OBJECT_DIR)netfs_client.o
	$(CC) $^ -o $@ $(LIBS)

$(OBJECT_DIR)netfs_client.o: $(SRC_DIR)netfs_client.c
	$(CC) $(CFLAGS_DEBUG) $< -o $@

clean:
	rm $(OBJECT_DIR)*

test:
	if [ ! -d $(OBJECT_DIR) ]; then mkdir OBJECT_DIR; fi

	