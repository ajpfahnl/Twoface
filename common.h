#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#include <zlib.h>

// wrapper functions for system calls
void close_wrap(int fd, int num);
void write_wrap(int fd, const void *buf, size_t nbyte, const char *msg);
int read_wrap(int fd, void *buf, size_t nbyte, const char *msg);
void dup_wrap(int fd, int num);

// compression and decompression
int zcompress_new(void *tmp_buf, void *buf, size_t bytes_read, size_t buf_size);
int zcompress(int fd, void *buf, size_t bytes_read, size_t buf_size);
int zdecompress_old(void *buf, size_t bytes_read, size_t buf_size);
#endif
