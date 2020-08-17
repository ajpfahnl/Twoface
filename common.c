#include "common.h"

// wrapper functions for system calls
void close_wrap(int fd, int num) { //num is used as id for debugging
    if (close(fd) == -1) {
        fprintf(stderr, "Error closing (%d) fd %d: %s\n", fd, num, strerror(errno));
        exit(1);
    }
}

void write_wrap(int fd, const void *buf, size_t nbyte, const char *msg) { //msg used for debug
    if (write(fd, buf, nbyte) == -1) {
        fprintf(stderr, "Error writing (%s): %s\n", msg, strerror(errno));
        exit(1);
    }
}

int read_wrap(int fd, void *buf, size_t nbyte, const char *msg) {
    int rcount = read(fd, buf, nbyte);
    if (rcount == -1) {
        fprintf(stderr, "Error reading (%s): %s\n", msg, strerror(errno));
        exit(1);
    }
    return rcount;
}

void dup_wrap(int fd, int num) { // num is used as id for debugging
    if (dup(fd) == -1) {
        fprintf(stderr, "Error duplicating (%d) fd %d: %s\n", fd, num, strerror(errno));
        exit(1);
    }
}

// compress functions
void check_Z_OK(int status, char * msg) {
    if (status != Z_OK) {
        fprintf(stderr, "Error with %s: returned %d\n", msg, status);
        exit(1);
    }
}

void check_stream_error(int status, char * msg) {
    if (status == Z_STREAM_ERROR) {
        fprintf(stderr, "Error with %s: returned %d\n", msg, status);
        exit(1);
    }
}


int zcompress_new(void *tmp_buf, void *buf, size_t bytes_read, size_t buf_size) {
    int status;
    int new_bytes;
    unsigned char compress_buf[buf_size];
    z_stream compress_stream;
    
    compress_stream.zalloc = Z_NULL;
    compress_stream.zfree = Z_NULL;
    compress_stream.opaque = Z_NULL;
    
    compress_stream.avail_in = bytes_read;
    compress_stream.next_in = buf;
    
    status = deflateInit(&compress_stream, Z_DEFAULT_COMPRESSION);
    check_Z_OK(status, "deflateInit()");
    
    compress_stream.avail_out = buf_size;
    compress_stream.next_out = compress_buf;
    status = deflate(&compress_stream, Z_FINISH);
    check_stream_error(status, "deflate()");
    new_bytes = buf_size - compress_stream.avail_out;
    memcpy(tmp_buf, compress_buf, new_bytes);
    
    deflateEnd(&compress_stream);
    
    return new_bytes;
}

int zcompress(int fd, void *buf, size_t bytes_read, size_t buf_size) {
    int status;
    int new_bytes;
    unsigned char compress_buf[buf_size];
    z_stream compress_stream;
    
    compress_stream.zalloc = Z_NULL;
    compress_stream.zfree = Z_NULL;
    compress_stream.opaque = Z_NULL;
    
    compress_stream.avail_in = bytes_read;
    compress_stream.next_in = buf;
    
    if ((status = deflateInit(&compress_stream, Z_DEFAULT_COMPRESSION)) != Z_OK) {
        fprintf(stderr, "Error with deflateInit(): returned %d\n", status);
        exit(1);
    }
    
    do {
        compress_stream.avail_out = buf_size;
        compress_stream.next_out = compress_buf;
        status = deflate(&compress_stream, Z_FINISH);
        new_bytes = buf_size - compress_stream.avail_out;
        write_wrap(fd, compress_buf, new_bytes, "compress w");
    } while(compress_stream.avail_out == 0);
    
    deflateEnd(&compress_stream);
    
    return new_bytes;
}

int zdecompress_old(void *buf, size_t bytes_read, size_t buf_size) {
    int status;
    int new_bytes;
    unsigned char inflate_buf[buf_size];
    z_stream inflate_stream;
    
    inflate_stream.zalloc = Z_NULL;
    inflate_stream.zfree = Z_NULL;
    inflate_stream.opaque = Z_NULL;
    
    
    inflate_stream.avail_in = bytes_read;
    inflate_stream.next_in = buf;
    inflate_stream.avail_out = buf_size;
    inflate_stream.next_out = inflate_buf;
    
    
    status = inflateInit(&inflate_stream);
    check_Z_OK(status, "deflateInit()");
    
    status = inflate(&inflate_stream, Z_FINISH);
    check_stream_error(status, "inflate()");
    new_bytes = buf_size - inflate_stream.avail_out;
    inflateEnd(&inflate_stream);
    
    memcpy(buf, inflate_buf, buf_size);
    return new_bytes;
}

