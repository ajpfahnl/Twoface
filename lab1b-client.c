/*
 File: lab1b-client.c
 NAME: Arnold Pfahnl
 EMAIL: ajpfahnl@gmail.com
 ID: 305176399
 */
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <poll.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "common.h"
// has wrapper functions for system calls
// as well as functions for compression

// client data
static int portnum, sockfd;
static char * logname;

// socket structs
static struct sockaddr_in serv_addr;
struct hostent * server;

// log fd
static int log_fd;

// fd[0] is for stdin and fd[1] is for server
static struct pollfd fds[2];

// getopt_long options
static struct option longopts[] = {
    {"port", required_argument, NULL, 'p'},
    {"log", required_argument, NULL, 'l'},
    {"compress", no_argument, NULL, 'c'},
    { NULL, 0, NULL, 0}
};

// Data structures for saving the current state of the terminal and
// an altered state (character-at-a-time, no-echo mode)
static struct termios saved_config;
static struct termios new_config;

// restores original terminal settings
void term_reset () {
    // reset back to original saved configuration
    if (tcsetattr(0, TCSANOW, &saved_config) == -1) {
        fprintf(stderr, "Error restoring terminal state: %s\n", strerror(errno));
        exit(1);
    }
}

// puts keyboard into character-at-a-time, no-echo mode
void term_adjust () {
    // get the current terminal modes
    if (tcgetattr(0, &saved_config) == -1){
        fprintf(stderr, "Error getting current terminal modes for keyboard (fd 0, client): %s\n", strerror(errno));
        exit(1);
    }
    
    // set the function for returning back to original terminal settings
    atexit(term_reset);
    
    // create copy of terminal modes and change config
    memcpy(&new_config, &saved_config, sizeof(struct termios));
    
    new_config.c_iflag = ISTRIP;	/* only lower 7 bits */
    new_config.c_oflag = 0;			/* no processing */
    new_config.c_lflag = 0;			/* no processing */
    
    if (tcsetattr(0, TCSANOW, &new_config) == -1) {
        fprintf(stderr, "Error putting keyboard into character-at-a-time, no-echo mode: %s\n", strerror(errno));
        exit(1);
    }
}

void client_socket() {
    // socket code mostly derived from the following tutorial
    // by Robert Ingalls:
    // http://www.cs.rpi.edu/~moorthy/Courses/os98/Pgms/socket.html
    
    // create new socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        exit(1);
    }
    
    // get network host entry
    server = gethostbyname("localhost");
    if (server == NULL) {
        fprintf(stderr, "Error getting host by name: %s\n", hstrerror(h_errno));
        exit(1);
    }
    
    // set fields in serv_addr
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(portnum);
    
    // connect to server
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Error initiating connection on socket: %s\n", strerror(errno));
        exit(1);
    }
}

void log_sent (char * log_str_sent, int bytes_sent) {
    dprintf(log_fd, "SENT %d bytes: ", bytes_sent);
    write(log_fd, log_str_sent, bytes_sent);
    write(log_fd, "\n", 1);
}

void log_received (char * log_str_receive, int bytes_received) {
    dprintf(log_fd, "RECEIVED %d bytes: ", bytes_received);
    write(log_fd, log_str_receive, bytes_received);
    write(log_fd, "\n", 1);
}

// reading and writing
void term_rw (bool log_set, bool compress_set) {
    size_t buf_size = 256*2;
    char buf_to[buf_size];
    char buf_from[buf_size];
    char cr_lf[] = {0x0D, 0x0A};
    int rcount_stdin, rcount_server;
    
    int def_bytes = 0;
    char tmp_buf[buf_size];
    while(1){
        // POLL
        if (poll(fds, 2, 0) == -1) {
            fprintf(stderr, "Error polling: %s\n", strerror(errno));
            exit(1);
        }
        
        // READ from keyboard
        rcount_stdin = -1;
        if (fds[0].revents & POLLIN) {
            rcount_stdin = read_wrap(0, buf_to, buf_size, "from stdin [1]");
        }
        
        // READ from server
        rcount_server = -1;
        if (fds[1].revents & POLLIN) {
            rcount_server = read_wrap(sockfd, buf_from, buf_size, "from server [1]");
        }
        
        // LOG received bytes
        if (log_set && rcount_server > 0) {
            log_received(buf_from, rcount_server);
        }
        
        // decompress
        
        if (compress_set) {
            if (rcount_server > 0) {
                rcount_server = zdecompress_old(buf_from, rcount_server, buf_size);
            }
        }
        
        // WRITE stdin to...
        char c;
        int i;
        for (i = 0; i < rcount_stdin; i++) {
            c = buf_to[i];
            // ...stdout
            switch (c) {
                case 0x0D: // <cr>
                case 0x0A: // <lf>
                    write_wrap(1, cr_lf, 2, "to display [1]");
                    break;
                default:
                    write_wrap(1, &c, 1, "to display [1]");
            }
            // ...and convert <cr> for write to server
            switch (c) {
                case 0x0D: // <cr>
                    buf_to[i] = 0x0A; // <lf>
                    break;
            }
        }
        
        //WRITE server read to stdout
        for (i = 0; i < rcount_server; i++) {
            c = buf_from[i];
            switch (c) {
                case 0x0A: // <lf>
                    write_wrap(1, cr_lf, 2, "to display [2]");
                    break;
                default:
                    write_wrap(1, &c, 1, "to display [2]");
            }
        }
        
        //WRITE stdin to server
        def_bytes = 0;
        if (rcount_stdin > 0) {
            if (compress_set) {
                def_bytes = zcompress_new(tmp_buf,buf_to, rcount_stdin, buf_size);
                write_wrap(sockfd, tmp_buf, def_bytes, "compress server");
            }
            else {
                write_wrap(sockfd, buf_to, rcount_stdin, "to server [1]");
            }
        }
        
        // LOGGING bytes written to server
        if (log_set) {
            if (compress_set && def_bytes > 0) {
                log_sent(tmp_buf, def_bytes);
            }
            else if (rcount_stdin > 0) {
                log_sent(buf_to, rcount_stdin);
            }
        }
        
        // SHUTDOWN
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            break;
        }
        if (fds[0].revents & (POLLHUP | POLLERR)) {
            break;
        }
        if (rcount_server == 0 || rcount_stdin == 0) {
            break;
        }
        
    }
    
}

/*
 PROGRAM BEGINS
 */
int main(int argc, char * argv[]) {
    bool port_set = false;
    bool log_set = false;
    bool compress_set = false;
    
    int opt;
    while((opt = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                port_set = true;
                portnum = atoi(optarg);
                break;
            case 'l':
                log_set = true;
                logname = optarg;
                break;
            case 'c':
                compress_set = true;
                break;
            default:
                fprintf(stderr, "usage: ./lab1b-client --port=<num> [--log=<filename>] [--compress]\n");
                exit(1);
        }
    }
    // --port is mandatory
    if (!port_set) {
        fprintf(stderr, "usage: ./lab1b-client --port=<num> [--log=<filename>] [--compress]\n");
        exit(1);
    }
    
    // set log
    if (log_set) {
        //if ((log_fd = creat(logname, 0666)) == -1) {
        if ((log_fd = open(logname, O_CREAT|O_WRONLY|O_APPEND, 0666)) == -1) {
            fprintf(stderr, "Error open/create log: %s\n", strerror(errno));
            exit(1);
        }
    }
    
    client_socket();
    
    //set polls
    fds[0].fd = 0;
    fds[0].events = POLLIN | POLLHUP | POLLERR;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN | POLLHUP | POLLERR;
    
    //terminal
    term_adjust();
    term_rw(log_set, compress_set);
    term_reset();
    exit(0);
}
