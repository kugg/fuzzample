#define _GNU_SOURCE
// Compile
// gcc -fPIC -shared server_ld_preload.c -o slp.so -ldl
// Usage: 
// LD_PRELOAD=./slp.so ./server
// AFL_PRELOAD=./slp.so afl-fuzz .... -- ./server

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#define MAGIC_SOCK_FD 254
//
// originals
//
int (*original_close)(int);
int (*original_select)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
ssize_t (*original_recv)(int, void *, size_t, int);
ssize_t (*original_send)(int sockfd, const void *buf, size_t len, int flags);

static int listener_fd = -1;

__attribute__((constructor)) void prep_orig()
{
    original_close = dlsym(RTLD_NEXT, "close");
    original_select = dlsym(RTLD_NEXT, "select");
    original_recv = dlsym(RTLD_NEXT, "recv");
    original_send = dlsym(RTLD_NEXT, "send");
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return MAGIC_SOCK_FD;
}
int bind(int sockfd, const struct sockaddr *addr,
                socklen_t addrlen) {
    listener_fd = sockfd;
    return 0;
}
/*
// Do something like this for clients
int socket(int domain, int type, int protocol) {
    return MAGIC_SOCK_FD;
}
int connect(int sockfd, const struct sockaddr *addr,
                   socklen_t addrlen) {
    input_data_fd = STDIN_FILENO;
    return 0;
}*/

int select(int nfds, fd_set *readfds, fd_set *writefds,
                  fd_set *exceptfds, struct timeval *timeout) {
    printf("select: magic socket\n");                 
    if (readfds != NULL) {
        int is_socket = nfds > MAGIC_SOCK_FD && FD_ISSET(MAGIC_SOCK_FD, readfds);
        int is_listener = nfds > listener_fd && FD_ISSET(listener_fd, readfds);
        if (is_socket || is_listener) {
            if (writefds != NULL) {
                FD_ZERO(writefds);
            }
            if (exceptfds != NULL) {
                FD_ZERO(exceptfds);
            }
            FD_ZERO(readfds);
            if (is_socket) {
                FD_SET(MAGIC_SOCK_FD,readfds);
            } else {
                FD_SET(listener_fd,readfds);
            }
            return 1;
        }
   
    }
    
    if (writefds != NULL) {
        int is_socket = nfds > MAGIC_SOCK_FD && FD_ISSET(MAGIC_SOCK_FD, writefds);
        if (is_socket) {
            if (readfds != NULL) {
                FD_ZERO(readfds);
            }
            if (exceptfds != NULL) {
                FD_ZERO(exceptfds);
            }
            FD_ZERO(writefds);
            
            FD_SET(MAGIC_SOCK_FD,writefds);
            return 1;
        }
    }
    
    return original_select(nfds, readfds, writefds, exceptfds, timeout);
   
}


ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    printf("recv: fd: %d\n",sockfd);
    if (sockfd == MAGIC_SOCK_FD) {
        // TODO: Handle special flags such as PEEK
        return read(STDIN_FILENO, buf,len);
    }
    return original_recv(sockfd, buf, len, flags);
}
ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    printf("send: fd: %d\n",sockfd);
    if (sockfd == MAGIC_SOCK_FD) {
        return write(STDOUT_FILENO, buf,len);
    }
    return original_send(sockfd, buf, len, flags);
}

int close(int fd) {
    if (fd == MAGIC_SOCK_FD) {
        exit(0);
        return 0;
    }
    return original_close(fd);
}
