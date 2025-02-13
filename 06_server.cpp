#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <fcntl.h>
#include <vector>
#include <poll.h>

const size_t k_max_msg = 4096;

enum
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

struct Conn
{
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
    // exit(1);
}

static void fd_set_nb(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        die("fcntl error");
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        die("fcntl error");
    }
}

static void conn_put(std::vector<Conn *> &fd2conn, Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static bool try_fill_buffer(Conn *conn);
static bool try_one_request(Conn *conn);
static void state_req(Conn *conn);
static void state_res(Conn *conn);
static bool try_flush_buffer(Conn *conn);

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd)
{
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }
        die("accept() error");
    }

    fd_set_nb(connfd);

    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;

    conn_put(fd2conn, conn);
    return 0;
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0);
    }
}

static bool try_fill_buffer(Conn *conn)
{
    assert(conn->rbuf_size <= sizeof(conn->rbuf));
    ssize_t rv = 0;
    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0)
    {
        if (errno == EAGAIN)
        {
            return false;
        }
        die("read() error");
        conn->state = STATE_END;
        return false;
    }

    if (rv == 0)
    {
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while (try_one_request(conn))
    {
    }

    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
    {
    }
}

static bool try_one_request(Conn *conn)
{
    if (conn->rbuf_size < 4)
    {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg)
    {
        conn->state = STATE_END;
        return false;
    }

    if (4 + len > conn->rbuf_size)
    {
        return false;
    }

    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    size_t remain = conn->rbuf_size - 4 - len;
    if (remain)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;
    conn->state = STATE_RES;
    state_res(conn);

    return (conn->state == STATE_REQ);
}

static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;
    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0)
    {
        if (errno == EAGAIN)
        {
            return false;
        }
        die("write() error");
        conn->state = STATE_END;
        return false;
    }

    conn->wbuf_sent += rv;

    if (conn->wbuf_sent == conn->wbuf_size)
    {
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true;
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
    {
    }
}

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket() error");
    }
    int val = 1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    /* takes fd, SOL_SOCKET is the level where the option resides(socker layer),
    SO_REUSEADDR allows reuse of local addrs
    val is 1 to enable the option in this case so_reuseaddr will be enabled
    sizeof(val) size of the option value to tell the function about the number of bytes to expect for the option*/

    struct sockaddr_in addr = {}; // is a struct from netinet/in.h

    addr.sin_family = AF_INET; // socket family is AF_INET

    addr.sin_port = ntohs(3000); // port

    addr.sin_addr.s_addr = ntohl(0); // ip of the server in this case it is 0.0.0.0

    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr)); // bind function binds the socket to the above ips and port

    if (rv)
    {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN); // listens on the socket that was bound by the bind function, SOMAXXCONN is the max amount of backlog the socket can have
    if (rv)
    {
        die("listen()");
    }

    printf("Server is listening on port 3000\n");
    std::vector<Conn *> fd2conn;
    fd_set_nb(fd);

    while (true)
    {
        (void)accept_new_conn(fd2conn, fd);
    }

    return 0;
}
