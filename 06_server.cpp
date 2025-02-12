#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include<fcntl.h>
#include <vector>
#include <poll.h>
const size_t k_max_msg = 4096;

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

struct Conn
{
    /* data */  
    int fd = -1;
    uint32_t state = 0;

    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];

    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

void die(const char *message)
{
    perror(message);
    exit(1);
}

static void fd_set_nb(int fd){
    errno = 0;
    int flags = fcntl(fd, F_GETFL,0);
    if (errno){
        die("fcntl error");
        return;
    }

    flags = O_NONBLOCK;

    errno = 0;

    errno = fcntl(fd, F_SETFL, flags);
    if (errno){
        die("fcntl error");
    }

}

int main(){

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd <0 ){
        die("socket() error");
        

    }
    std::vector<Conn*> fd2conn;
    fd_set_nb(fd);

    std::vector<struct pollfd> poll_args;

    while(true){
        poll_args.clear();
        struct pollfd pfd = {fd,POLLIN,0};

        poll_args.push_back(pfd);

        for (Conn* conn : fd2conn){
            if (!conn) continue;

            struct pollfd pfd = {};
            pfd.fd = conn -> fd;
            pfd.events = (conn -> state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd); 
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(),1000);
        if (rv < 0){
            die("poll");
        }2
    }
    return 0;
}