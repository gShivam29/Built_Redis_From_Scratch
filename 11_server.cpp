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
#include <unordered_map>
#include "hashtable.h"
#include "hashtable.cpp"
#include "zset.h"
#include "zset.cpp"
#include <algorithm>
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

enum
{
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
    SER_DBL = 5, // Added for double values
};

enum
{
    T_STR = 0,  // Plain string type
    T_ZSET = 1, // Sorted set type
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

// Base entry type with type indicator
struct Entry
{
    struct HNode node;
    std::string key;
    int type = T_STR; // Default to string type
};

// String entry
struct StrEntry : public Entry
{
    std::string val;

    StrEntry()
    {
        type = T_STR;
    }
};

// ZSET entry
struct ZSetEntry : public Entry
{
    Zset zset;

    ZSetEntry()
    {
        type = T_ZSET;
    }
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

// Output serializers
static void out_nil(std::string &out)
{
    out.push_back(SER_NIL);
}

static void out_str(std::string &out, const std::string &val)
{
    out.push_back(SER_STR);
    uint32_t len = (uint32_t)val.size();
    out.append((char *)&len, 4);
    out.append(val);
}

static void out_int(std::string &out, int64_t val)
{
    out.push_back(SER_INT);
    out.append((char *)&val, 8);
}

static void out_dbl(std::string &out, double val)
{
    out.push_back(SER_DBL);
    out.append((char *)&val, sizeof(double));
}

static void out_err(std::string &out, int32_t code, const std::string &msg)
{
    out.push_back(SER_ERR);
    out.append((char *)&code, 4);
    uint32_t len = (uint32_t)msg.size();
    out.append((char *)&len, 4);
    out.append(msg);
}

static void out_arr(std::string &out, uint32_t n)
{
    out.push_back(SER_ARR);
    out.append((char *)&n, 4);
}

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg)
{
    if (tab->size == 0)
    {
        return;
    }

    for (size_t i = 0; i < tab->mask + 1; ++i)
    {
        HNode *node = tab->tab[i];
        while (node)
        {
            f(node, arg);
            node = node->next;
        }
    }
}

static void cb_scan(HNode *node, void *arg)
{
    std::string &out = *(std::string *)arg;
    out_str(out, container_of(node, Entry, node)->key);
}

size_t hm_size(const HMap *hmap)
{
    // Sum the sizes of the two hash tables
    return hmap->ht1.size + hmap->ht2.size;
}

static bool entry_eq(HNode *lhs, HNode *rhs)
{
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return lhs->hcode == rhs->hcode && le->key == re->key;
}

// Command handlers
static void do_keys(std::vector<std::string> &cmd, std::string &out)
{
    (void)cmd;

    out_arr(out, (uint32_t)hm_size(&g_data.db));
    h_scan(&g_data.db.ht1, &cb_scan, &out);
    h_scan(&g_data.db.ht2, &cb_scan, &out);
}

static void do_del(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        Entry *entry = container_of(node, Entry, node);
        if (entry->type == T_ZSET)
        {
            delete (ZSetEntry *)entry;
        }
        else
        {
            delete (StrEntry *)entry;
        }
        out_int(out, 1);
    }
    else
    {
        out_int(out, 0);
    }
}

static void do_get(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node)
    {
        out_nil(out);
        return;
    }

    Entry *entry = container_of(node, Entry, node);
    if (entry->type != T_STR)
    {
        out_err(out, RES_ERR, "Expecting string type");
        return;
    }

    StrEntry *str_entry = (StrEntry *)entry;
    out_str(out, str_entry->val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        Entry *entry = container_of(node, Entry, node);
        if (entry->type != T_STR)
        {
            // Remove the old entry of different type
            hm_pop(&g_data.db, &key.node, &entry_eq);
            if (entry->type == T_ZSET)
            {
                delete (ZSetEntry *)entry;
            }
            else
            {
                delete (StrEntry *)entry;
            }

            // Create a new string entry
            StrEntry *new_entry = new StrEntry();
            new_entry->key.swap(key.key);
            new_entry->node.hcode = key.node.hcode;
            new_entry->val.swap(cmd[2]);
            hm_insert(&g_data.db, &new_entry->node);
        }
        else
        {
            // Update existing string
            StrEntry *str_entry = (StrEntry *)entry;
            str_entry->val.swap(cmd[2]);
        }
    }
    else
    {
        // Create new string entry
        StrEntry *new_entry = new StrEntry();
        new_entry->key.swap(key.key);
        new_entry->node.hcode = key.node.hcode;
        new_entry->val.swap(cmd[2]);
        hm_insert(&g_data.db, &new_entry->node);
    }
    out_str(out, "OK");
}

// ZSET Commands

// ZADD key score member [score member ...]
static void do_zadd(std::vector<std::string> &cmd, std::string &out)
{
    if ((cmd.size() - 2) % 2 != 0)
    {
        out_err(out, RES_ERR, "ZADD requires pairs of score and member");
        return;
    }

    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    ZSetEntry *zset = nullptr;
    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        Entry *entry = container_of(node, Entry, node);
        if (entry->type != T_ZSET)
        {
            // Replace existing entry with a new ZSET
            hm_pop(&g_data.db, &key.node, &entry_eq);
            if (entry->type == T_STR)
            {
                delete (StrEntry *)entry;
            }
            else
            {
                delete entry;
            }

            zset = new ZSetEntry();
            zset->key.swap(key.key);
            zset->node.hcode = key.node.hcode;
            hm_insert(&g_data.db, &zset->node);
        }
        else
        {
            zset = (ZSetEntry *)entry;
        }
    }
    else
    {
        // Create new ZSET
        zset = new ZSetEntry();
        zset->key.swap(key.key);
        zset->node.hcode = key.node.hcode;
        hm_insert(&g_data.db, &zset->node);
    }

    int64_t added = 0;
    // Process score-member pairs
    for (size_t i = 2; i < cmd.size(); i += 2)
    {
        double score = std::stod(cmd[i]);
        const std::string &member = cmd[i + 1];
        if (zset_add(&zset->zset, member.data(), member.size(), score))
        {
            added++;
        }
    }

    out_int(out, added);
}

// ZSCORE key member
static void do_zscore(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node)
    {
        out_nil(out);
        return;
    }

    Entry *entry = container_of(node, Entry, node);
    if (entry->type != T_ZSET)
    {
        out_err(out, RES_ERR, "Expecting ZSET type");
        return;
    }

    ZSetEntry *zset = (ZSetEntry *)entry;
    const std::string &member = cmd[2];
    Znode *znode = zset_lookup(&zset->zset, member.data(), member.size());

    if (znode)
    {
        out_dbl(out, znode->score);
    }
    else
    {
        out_nil(out);
    }
}

// ZRANGE key start stop [WITHSCORES]
static void do_zrange(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node)
    {
        out_arr(out, 0);
        return;
    }

    Entry *entry = container_of(node, Entry, node);
    if (entry->type != T_ZSET)
    {
        out_err(out, RES_ERR, "Expecting ZSET type");
        return;
    }

    ZSetEntry *zset = (ZSetEntry *)entry;

    int64_t start = std::stoll(cmd[2]);
    int64_t stop = std::stoll(cmd[3]);

    // Check for WITHSCORES option
    bool withscores = (cmd.size() > 4 && cmd[4] == "WITHSCORES");

    // Get the size of the sorted set
    int64_t size = 0;
    if (zset->zset.tree)
    {
        size = zset->zset.tree->cnt;
    }

    // Handle negative indices (relative to the end)
    if (start < 0)
        start = size + start;
    if (stop < 0)
        stop = size + stop;

    // Boundary checks
    if (start < 0)
        start = 0;
    if (stop >= size)
        stop = size - 1;
    if (start > stop || start >= size)
    {
        out_arr(out, 0);
        return;
    }

    int64_t count = stop - start + 1;
    out_arr(out, withscores ? count * 2 : count);

    // Find the first element
    Znode *node_start = nullptr;
    if (zset->zset.tree)
    {
        AVLNode *avl = zset->zset.tree;

        // Get the leftmost node (lowest score)
        while (avl->left)
        {
            avl = avl->left;
        }

        // Move to the start position
        if (start > 0)
        {
            avl = avl_offset(avl, start);
        }

        if (avl)
        {
            node_start = container_of(avl, Znode, tree);
        }
    }

    // Output the range
    AVLNode *current = node_start ? &node_start->tree : nullptr;
    for (int64_t i = 0; i < count && current; ++i)
    {
        Znode *z = container_of(current, Znode, tree);

        // Output member
        out_str(out, std::string(z->name, z->len));

        // Output score if requested
        if (withscores)
        {
            out_dbl(out, z->score);
        }

        // Move to next node
        if (current->right)
        {
            current = current->right;
            while (current->left)
            {
                current = current->left;
            }
        }
        else
        {
            // Backtrack to find the next node
            AVLNode *parent = current->parent;
            while (parent && parent->right == current)
            {
                current = parent;
                parent = current->parent;
            }
            current = parent;
        }
    }
}

// Process request based on command
static void do_request(std::vector<std::string> &cmd, std::string &out)
{
    if (cmd.empty())
    {
        out_err(out, RES_ERR, "Empty command");
        return;
    }

    std::string &command = cmd[0];

    // Convert command to lowercase
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    // Handle commands
    if (command == "keys")
    {
        do_keys(cmd, out);
    }
    else if (command == "get" && cmd.size() == 2)
    {
        do_get(cmd, out);
    }
    else if (command == "set" && cmd.size() == 3)
    {
        do_set(cmd, out);
    }
    else if (command == "del" && cmd.size() == 2)
    {
        do_del(cmd, out);
    }
    // ZSET commands
    else if (command == "zadd" && cmd.size() >= 4)
    {
        do_zadd(cmd, out);
    }
    else if (command == "zscore" && cmd.size() == 3)
    {
        do_zscore(cmd, out);
    }
    else if ((command == "zrange" && cmd.size() >= 4 && cmd.size() <= 5))
    {
        do_zrange(cmd, out);
    }
    else
    {
        out_err(out, RES_ERR, "Unknown command or wrong number of arguments");
    }
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
        printf("Message too large from fd %d: %u > %zu\n", conn->fd, len, k_max_msg);
        conn->state = STATE_END;
        return false;
    }

    if (4 + len > conn->rbuf_size)
    {
        return false; // Incomplete message, need more data
    }

    // Parse the request and generate a response
    std::vector<std::string> cmd;
    if (0 != parse_req(&conn->rbuf[4], len, cmd))
    {
        msg("bad req");
        conn->state = STATE_END;
        return false;
    }

    // Log the command
    if (!cmd.empty())
    {
        printf("Command: %s", cmd[0].c_str());
        for (size_t i = 1; i < std::min(cmd.size(), size_t(3)); ++i)
        {
            printf(" %s", cmd[i].c_str());
        }
        if (cmd.size() > 3)
            printf(" ...");
        printf("\n");
    }

    // Generate the response
    std::string out;
    do_request(cmd, out);

    if (4 + out.size() > k_max_msg)
    {
        out.clear();
        out_err(out, RES_ERR, "response is too big");
    }

    uint32_t wlen = (uint32_t)out.size();
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], out.data(), out.size());
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

// Cleanup database entries when server is shutting down
static void db_cleanup()
{
    // Free all entries in ht1
    for (size_t i = 0; i < g_data.db.ht1.mask + 1; ++i)
    {
        HNode *node = g_data.db.ht1.tab ? g_data.db.ht1.tab[i] : nullptr;
        while (node)
        {
            HNode *next = node->next;
            Entry *entry = container_of(node, Entry, node);
            if (entry->type == T_ZSET)
            {
                delete (ZSetEntry *)entry;
            }
            else
            {
                delete (StrEntry *)entry;
            }
            node = next;
        }
    }

    // Free all entries in ht2
    for (size_t i = 0; i < g_data.db.ht2.mask + 1; ++i)
    {
        HNode *node = g_data.db.ht2.tab ? g_data.db.ht2.tab[i] : nullptr;
        while (node)
        {
            HNode *next = node->next;
            Entry *entry = container_of(node, Entry, node);
            if (entry->type == T_ZSET)
            {
                delete (ZSetEntry *)entry;
            }
            else
            {
                delete (StrEntry *)entry;
            }
            node = next;
        }
    }

    // Free hash table structures
    free(g_data.db.ht1.tab);
    free(g_data.db.ht2.tab);
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
    printf("Supported commands:\n");
    printf("  GET key\n");
    printf("  SET key value\n");
    printf("  DEL key\n");
    printf("  KEYS\n");
    printf("  ZADD key score member [score member ...]\n");
    printf("  ZSCORE key member\n");
    printf("  ZRANGE key start stop [WITHSCORES]\n");

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
                continue; // Skip empty slots

            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events |= POLLERR; // Always check for errors
            poll_args.push_back(pfd);
        }

        // Wait for events
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000); // 1 second timeout
        if (rv < 0)
        {
            die("poll() error");
            continue;
        }

        // Process events
        for (size_t i = 0; i < poll_args.size(); ++i)
        {
            if (poll_args[i].revents == 0)
                continue; // No events

            if (poll_args[i].fd == fd)
            {
                // Server socket event - new connection
                if (poll_args[i].revents & POLLIN)
                {
                    accept_new_conn(fd2conn, fd);
                }
            }
            else
            {
                // Client socket event
                Conn *conn = nullptr;
                size_t fd_idx = 0;

                // Find the connection
                for (fd_idx = 0; fd_idx < fd2conn.size(); ++fd_idx)
                {
                    if (fd2conn[fd_idx] && fd2conn[fd_idx]->fd == poll_args[i].fd)
                    {
                        conn = fd2conn[fd_idx];
                        break;
                    }
                }

                if (!conn)
                    continue; // Connection not found

                // Handle errors
                if (poll_args[i].revents & (POLLERR | POLLHUP))
                {
                    conn->state = STATE_END;
                    continue;
                }

                // Handle read/write events
                if ((poll_args[i].revents & POLLIN) && conn->state == STATE_REQ)
                {
                    connection_io(conn);
                }
                else if ((poll_args[i].revents & POLLOUT) && conn->state == STATE_RES)
                {
                    connection_io(conn);
                }
            }
        }
    }

    // Cleanup before exiting (this part won't be reached in normal operation)
    close(fd);
    db_cleanup();

    return 0;
}