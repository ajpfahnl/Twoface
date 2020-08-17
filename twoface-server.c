#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <poll.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "common.h"
// has wrapper functions for system calls
// as well as functions for compression

// macros for pipes
#define READ 0
#define WRITE 1

// getopt_long options
static struct option longopts[] = {
    {"port", required_argument, NULL, 'p'},
    {"shell", required_argument, NULL, 's'},
    {"compress", no_argument, NULL, 'c'},
    { NULL, 0, NULL, 0}
};

// fd[0] is for fd of socket connection and fd[1] is for pipe that returns
// output from the shell. Initialized when option --shell is specified
static struct pollfd fds[2];

// File desciptors for forwarding to child process and reading from
// child process. These are initialized in the parent process when
// option --shell is specified.
static int forward_fd;
static int read_fd;

// PID of the shell process (if option --shell is used)
static pid_t child_pid;

// server data
static int sockfd, newsockfd, portnum, clilen;
struct sockaddr_in serv_addr, cli_addr;

// handler for SIGPIPE
// turns received_sigpipe to true which causes term_rw() to stop
// read-write
static bool received_sigpipe = false;
void handler(int sigint) {
    if (sigint == SIGPIPE) {
        received_sigpipe = true;
    }
}

// set server socket
void server_socket() {
    // socket code mostly derived from the following tutorial
    // by Robert Ingalls:
    // http://www.cs.rpi.edu/~moorthy/Courses/os98/Pgms/socket.html
    
    // create new socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        exit(1);
    }
    
    // set fields of serv_addr
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portnum);
    
    // bind socket to address
    if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        fprintf(stderr, "Error binding socket to address: %s\n", strerror(errno));
        exit(1);
    }
    
    // listen for connections
    listen(sockfd, 5);
    
    // block until client connects to server
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, (socklen_t*)&clilen);
    if(newsockfd == -1) {
        fprintf(stderr, "Error establishing connection with client: %s\n", strerror(errno));
        exit(1);
    }
}

// reading and writing with additional conditions
void term_rw (bool forwarding, bool compress_set) {
    size_t buf_size = 256*2;
    char buf[buf_size];
    char buf_shellin[buf_size];
    int rcount, rcount_shellin;
    bool escape = false;
    char lf[] = {0x0A};
    bool forward_fd_open = true;
    bool read_fd_open = true;
    bool shutdown = false;
    
    char tmp_buf[buf_size];
    int def_bytes;
    
    while(1){
        /*
         * -------------------- POLL -------------------- *
         */
        
        // poll for option --shell
        if (forwarding) {
            if (poll(fds, 2, 0) == -1) {
                fprintf(stderr, "Error polling: %s\n", strerror(errno));
                exit(1);
            }
        }
        
        /*
         * -------------------- READ -------------------- *
         */
        
        // read from client
        // for option --shell, only read if POLLIN in revents
        rcount = -1;
        if (!forwarding || fds[0].revents & POLLIN) {
            rcount = read_wrap(newsockfd, buf, buf_size, "from client [1]");
        }
        
        // for option --shell, read from shell
        rcount_shellin = -1;
        if (forwarding) {
            if (fds[1].revents & POLLIN) {
                rcount_shellin = read_wrap(read_fd, buf_shellin, buf_size, "from shell [1]");
            }
        }
        /*
         DEAL WITH COMPRESSION
         */
        if (compress_set) {
            if (rcount > 0) {
                rcount = zdecompress_old(buf, rcount, buf_size);
            }
        }
        /*
         * -------------------- shutdown check -------------------- *
         */
        
        // if EOF or polling-error from shell, shut down
        if (forwarding) {
            if (rcount_shellin == 0 ||
                fds[1].revents & POLLHUP ||
                fds[1].revents & POLLERR) {
                
                shutdown = true;
            }
            if (rcount == 0 ||
                fds[0].revents & POLLHUP ||
                fds[0].revents & POLLERR) {
                
                shutdown = true;
            }
        }
        
        /*
         
         no option write back to client
         
         */
        
        if (!forwarding && rcount > 0) {
            if (compress_set) {
                zcompress(newsockfd, buf, rcount, buf_size);
            }
            else {
                write_wrap(newsockfd, buf, rcount, "to client");
            }
        }
        
        /*                     KEYBOARD
         *           CHECK FOR SPECIAL CHARACTERS
         *                       and
         * -------------------- WRITE -------------------- *
         */
        char c;
        int i;
        for (i = 0; i < rcount; i++) {
            c = buf[i];
            switch (c)
            {
                    // CLIENT escape sequence check
                    //
                    //  1) check for escape sequence -- 0x04 is hex for ^D escape sequence
                    //     (no --shell option)
                    //  2) check for ^C (0x03), use kill(2) to send SIGINT to shell process
                    //  3) close pipe to shell if receive ^D (0x04)
                case 0x04: // ^D
                    if (!forwarding) { escape = true; break; }
                    if (forwarding) {
                        close_wrap(forward_fd, 20);
                        forward_fd_open = false;
                    }
                    break;
                case 0x03: // ^C
                    if (forwarding) {
                        if (kill(child_pid, SIGINT)==-1) {
                            fprintf(stderr, "Error with kill: %s\n", strerror(errno));
                            exit(1);
                        }
                    }
                    break;
                    //
                    // KEYBOARD WRITE
                    // 1) <cr> or <lf> mapping write
                    //    mapping to shell, only <lf>
                    //
                    // 2) normal write
                    //    forward to shell
                    // *** We also check for SIGPIPE here
                case 0x0D: // <cr>
                case 0x0A: // <lf>
                    if (forwarding && forward_fd_open) {
                        write_wrap(forward_fd,lf,1, "to shell [1]");
                    }
                    break;
                default:
                    if (forwarding && forward_fd_open) {
                        write_wrap(forward_fd, &buf[i],1, "to shell [2]");
                    }
            }
            
            /* ---------- check for SIGPIPE  ---------- */
            if (received_sigpipe) {
                close_wrap(forward_fd, 1001);
                forward_fd_open = false;
                shutdown = true;
            }
        }
        
        // stop when no --shell option due to ^D
        // look at "KEYBOARD escape sequence check" 1)
        if (escape) {
            break;
        }
        
        /*
         * -------------------- SHELL WRITE -------------------- *
         */
        
        // SHELL INPUT forward to client
        
        if (rcount_shellin != -1) {
            if (compress_set) {
                def_bytes = zcompress_new(tmp_buf, buf_shellin, rcount_shellin, buf_size);
                write_wrap(newsockfd, tmp_buf, def_bytes, "to client");
            }
            else {
                write_wrap(newsockfd, buf_shellin, rcount_shellin, "to client");
            }
        }
        
        /*
         * -------------------- SHUTDOWN -------------------- *
         */
        
        if (shutdown) {
            if (forward_fd_open) {
                close_wrap(forward_fd, 1000);
            }
            if (read_fd_open) {
                close_wrap(read_fd, 2000);
            }
            break;
        }
    }
}

/*
 PROGRAM BEGINS
 */
int main(int argc, char * argv[]) {
    bool port_set = false;
    bool shell_set = false;
    bool compress_set = false;
    
    char * program;
    int opt;
    while((opt = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                port_set = true;
                portnum = atoi(optarg);
                break;
            case 's':
                shell_set = true;
                program = optarg;
                break;
            case 'c':
                compress_set = true;
                break;
            default:
                fprintf(stderr, "usage: ./twoface-server --port=<num> [--shell=<program>] [--compress]\n");
                exit(1);
        }
    }
    
    // --port mandatory
    if (!port_set) {
        fprintf(stderr, "usage: ./twoface-server --port=<num> [--shell=<program>] [--compress]\n");
        exit(1);
    }
    
    // connect with client
    server_socket();
    
    // --shell mode
    if (shell_set) {
        // create pipes
        int pipe_in[2], pipe_out[2]; // pipes into shell and out of shell
        if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
            fprintf(stderr, "Error creating pipe: %s\n", strerror(errno));
            exit(1);
        }
        
        // fork process
        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "Error forking: %s\n", strerror(errno));
            exit(1);
        }
        
        // child process (shell)
        if (pid == 0) {
            // make stdin pipe from terminal process
            close_wrap(0, 0);
            dup_wrap(pipe_in[READ], 0);
            close_wrap(pipe_in[READ], 1);
            
            // make stdout and stderr dups of pipe to terminal
            close_wrap(1, 2);
            dup_wrap(pipe_out[WRITE], 2);
            close_wrap(2, 3);
            dup_wrap(pipe_out[WRITE], 3);
            close_wrap(pipe_out[WRITE], 4);
            
            //close the rest of the pipe file descriptors
            close_wrap(pipe_in[WRITE], 5);
            
            // create shell from child process
            char * args[] = {program, NULL};
            if (execvp(*args, args) == -1) {
                fprintf(stderr, "Error with execv: %s\n", strerror(errno));
                exit(1);
            }
        }
        // parent process (terminal)
        else {
            child_pid = pid;
            //set signal handler for SIGPIPE
            if (signal(SIGPIPE,handler) == SIG_ERR) {
                fprintf(stderr, "Error setting signal handler for SIGPIPE: %s\n", strerror(errno));
                exit(1);
            }
            //close pipe fds used by child
            close_wrap(pipe_in[READ], 7);
            close_wrap(pipe_out[WRITE], 8);
            // set read, write file descriptors
            forward_fd = pipe_in[WRITE];
            read_fd    = pipe_out[READ];
            //set polls
            fds[0].fd = newsockfd;
            fds[0].events = POLLIN | POLLHUP | POLLERR;
            fds[1].fd = read_fd;
            fds[1].events = POLLIN | POLLHUP | POLLERR;
            // terminal
            term_rw(true, compress_set);
            
            // wait for child process to end
            int status;
            if (waitpid(pid, &status, 0) == -1) {
                fprintf(stderr, "Error with waitpid: %s\n", strerror(errno));
                exit(1);
            }
            int child_exit_signal = WTERMSIG(status);
            // alternatively use: = 0x007f & status;
            
            int child_exit_status = WEXITSTATUS(status);
            // alternatively use:  = (0xff00 & status) >> 8;
            
            fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", child_exit_signal, child_exit_status);
            
            //close connection to socket
            shutdown(newsockfd, SHUT_RDWR);
        }
    }
    
    // no --shell mode
    else {
        term_rw(false, compress_set);
    }
    exit(0);
}
