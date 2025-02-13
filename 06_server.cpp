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

static void conn_put(std::vector<Conn*> &fd2conn, struct Conn *conn){
    if (fd2conn.size() <= (size_t) conn ->fd){
        fd2conn.resize(conn -> fd + 1);
    }
    fd2conn[conn -> fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn*> &fd2conn, int fd){
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *) &client_addr, &socklen);
    if (connfd < 0){
        die("accept() error");
        return -1;
    }

    fd_set_nb(connfd);

    struct Conn* conn = (struct Conn*)malloc(sizeof(struct Conn));
    if (!conn){
        close(connfd);
        return -1;
    }
    conn -> fd = connfd;
    conn -> state = STATE_REQ;
    conn -> rbuf_size = 0;
    conn -> wbuf_size = 0;
    conn -> wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

static void connection_io(Conn* conn){
    if (conn -> state == STATE_REQ){
        state_req(conn);
    } else if (conn -> state == STATE_RES){
        state_res(conn);
    } else {
        assert(0);
    }
}

int main(){

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd <0 ){
        die("socket() error");
        

    }

    std::vector<Conn*> fd2conn; //vector of connections

    fd_set_nb(fd);

    std::vector<struct pollfd> poll_args; //vector of struct pollfd for poll arguments 

 //event loop
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
        }

        for (size_t i =1; i < poll_args.size(); ++i){
            if (poll_args[i].revents){
                Conn* conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn -> state == STATE_END){
                    fd2conn[conn -> fd] = NULL;
                    (void) close (conn ->fd);
                    free(conn);
                }
            }
        }

        if (poll_args[0].revents){
            (void)accept_new_conn(fd2conn,fd);
        }
    }
    return 0;
}