#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <fcntl.h>
#include <vector>
#include <poll.h>
#include <errno.h>
#include <cstdlib>
#include <map>
#include "hashtable.h"
#include "hashtable.cpp"

const size_t k_max_msg = 4096;
const size_t k_max_args = 16;
enum
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,

};

enum
{
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};
struct Conn
{
    int fd = -1;
    uint32_t state = STATE_REQ;

    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];

    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

// the structure for the key
struct Entry
{
    struct HNode node;
    std::string key;
    std::string val;
};

static struct
{
    HMap db;
} g_data;

void die(const char *message)
{
    perror(message);
    // exit(1); // Do not exit immediately in production, just log and continue
}

void msg(const char *message)
{
    std::cout << message << std::endl;
}

static void fd_set_nb(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        die("fcntl F_GETFL error");
        return;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        die("fcntl F_SETFL error");
    }
}

static void conn_put(std::vector<Conn *> &fd2conn, Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1, nullptr); // Initialize with nullptr
    }
    fd2conn[conn->fd] = conn;
}

// Forward declarations
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
        return -1;
    }

    // Log client connection
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    printf("New connection from %s:%d, assigned fd %d\n",
           client_ip, ntohs(client_addr.sin_port), connfd);

    fd_set_nb(connfd); // Set the new socket to non-blocking

    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;

    conn_put(fd2conn, conn); // Save the connection in the list
    return 0;
}

static void connection_io(Conn *conn)
{
    if (!conn)
    {
        return; // Protect against null pointers
    }

    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else if (conn->state == STATE_END)
    {
        // Handle connection cleanup
    }
    else
    {
        assert(0); // Should never reach here
    }
}

static bool try_fill_buffer(Conn *conn)
{
    assert(conn->rbuf_size <= sizeof(conn->rbuf));
    ssize_t rv = 0;
    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        if (cap == 0)
        {
            // Buffer is full, cannot read more
            return false;
        }
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
        // Client closed connection
        printf("Client on fd %d closed connection\n", conn->fd);
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    // Process as many complete requests as possible
    while (try_one_request(conn))
    {
    }

    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn)) // Fill buffer and process request
    {
    }
}

static int32_t do_request(const uint8_t *req, uint32_t reqlen,
                          uint32_t *rescode, uint8_t *res, uint32_t *reslen);

static bool try_one_request(Conn *conn)
{
    if (conn->rbuf_size < 4)
    {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    // len = ntohl(len); // Convert from network byte order

    if (len > k_max_msg)
    {
        printf("Message too large from fd %d: %u > %zu\n", conn->fd, len, k_max_msg);
        conn->state = STATE_END;
        return false;
    }

    if (4 + len > conn->rbuf_size)
    {
        return false; // Incomplete message, need more data
    }

    printf("client says: %.*s\n", (int)len, &conn->rbuf[4]);

    // Prepare response (echo back the request)
    uint32_t len_network = htonl(len); // Convert to network byte order
    memcpy(&conn->wbuf[0], &len_network, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;
    conn->wbuf_sent = 0;

    // got one request, generate the response.
    uint32_t rescode = 0;
    uint32_t wlen = 0;
    int32_t err = do_request(
        &conn->rbuf[4], len,
        &rescode, &conn->wbuf[4 + 4], &wlen);
    if (err)
    {
        conn->state = STATE_END;
        return false;
    }
    wlen += 4;
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = 4 + wlen;
    // Remove the processed request from the buffer
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // Change state to response mode
    conn->state = STATE_RES;
    state_res(conn); // Try to send response immediately

    return (conn->state == STATE_REQ);
}

bool cmd_is(const std::string &cmd_str, const std::string &command)
{
    return cmd_str == command;
}
static bool entry_eq(HNode *lhs, HNode *rhs)
{
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return lhs->hcode == rhs->hcode && le->key == re->key;
}
static uint32_t do_get(std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node)
    {
        return RES_NX;
    }

    const std::string &val = container_of(node, Entry, node)->val;
    assert(val.size() <= k_max_msg);
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();
    return RES_OK;
}

static uint32_t do_set(std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        container_of(node, Entry, node)->val.swap(cmd[2]);
    }
    else
    {
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->val.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }

    return RES_OK;
}

static uint32_t do_del(std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);

    if (node)
    {
        delete container_of(node, Entry, node);
    }
    return RES_OK;
}

static int32_t parse_req(const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len < 4)
    {
        return -1;
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > k_max_args)
    {
        return -1;
    }

    size_t pos = 4;
    while (n--)
    {
        if (pos + 4 > len)
        {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len)
        {
            return -1;
        }
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }
    if (pos != len)
    {
        return -1;
    }
    return 0;
}

static int32_t do_request(const uint8_t *req, uint32_t reqlen,
                          uint32_t *rescode, uint8_t *res, uint32_t *reslen)
{

    std::vector<std::string> cmd;
    if (parse_req(req, reqlen, cmd) != 0)
    {
        printf("bad req");
        return -1;
    }

    if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
    {
        *rescode = do_get(cmd, res, reslen);
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
    {
        *rescode = do_set(cmd, res, reslen);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
    {
        *rescode = do_del(cmd, res, reslen);
    }
    else
    {
        *rescode = RES_ERR;
        const char *msg = "Unknow cmd";
        strcpy((char *)res, msg);
        *reslen = strlen(msg);
        return 0;
    }
    return 0;
}

static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;
    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        if (remain == 0)
        {
            // Nothing to send, switch back to receiving mode
            conn->state = STATE_REQ;
            conn->wbuf_sent = 0;
            conn->wbuf_size = 0;
            return false;
        }

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
        // Response fully sent, switch back to request mode
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true; // More data to send
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn)) // Flush buffer and send response
    {
    }
}

// Cleanup closed connections
static void conn_cleanup(std::vector<Conn *> &fd2conn)
{
    for (size_t i = 0; i < fd2conn.size(); ++i)
    {
        Conn *conn = fd2conn[i];
        if (!conn)
        {
            continue;
        }

        if (conn->state == STATE_END)
        {
            printf("Cleaning up connection fd %d\n", conn->fd);
            close(conn->fd);
            fd2conn[i] = nullptr;
            delete conn;
        }
    }
}

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket() error");
        return 1;
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);              // Port 3000
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to all IPs

    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if (rv)
    {
        die("bind() error");
        close(fd);
        return 1;
    }

    rv = listen(fd, SOMAXCONN); // Listen for incoming connections
    if (rv)
    {
        die("listen() error");
        close(fd);
        return 1;
    }

    printf("Server is listening on port 3000\n");

    std::vector<Conn *> fd2conn;
    fd_set_nb(fd); // Set server socket to non-blocking

    std::vector<struct pollfd> poll_args;

    while (true)
    {
        // Clean up closed connections first
        conn_cleanup(fd2conn);

        // Prepare for polling
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        // Add client connections to poll
        for (size_t i = 0; i < fd2conn.size(); ++i)
        {
            Conn *conn = fd2conn[i];
            if (!conn)
            {
                continue;
            }

            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            poll_args.push_back(pfd);
        }

        // Wait for events
        int timeout_ms = 1000; // 1 second timeout for poll
        int rv = poll(poll_args.data(), poll_args.size(), timeout_ms);
        if (rv < 0)
        {
            die("poll() error");
            continue;
        }

        // Check for timeout
        if (rv == 0)
        {
            continue; // Timeout occurred, just loop again
        }

        // Process events
        for (size_t i = 0; i < poll_args.size(); ++i)
        {
            if (poll_args[i].revents == 0)
            {
                continue; // No events for this fd
            }

            if (poll_args[i].fd == fd)
            {
                // Server socket has a new connection
                if (poll_args[i].revents & POLLIN)
                {
                    accept_new_conn(fd2conn, fd);
                }
            }
            else
            {
                // Client connection activity
                int client_fd = poll_args[i].fd;
                if (client_fd >= 0 && client_fd < (int)fd2conn.size() && fd2conn[client_fd])
                {
                    connection_io(fd2conn[client_fd]);
                }
            }
        }
    }

    // Clean up resources (this code is never reached in this example)
    for (auto conn : fd2conn)
    {
        if (conn)
        {
            close(conn->fd);
            delete conn;
        }
    }
    close(fd);

    return 0;
}