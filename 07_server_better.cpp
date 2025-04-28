#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <map>
#include <poll.h>
#include <fcntl.h>
#include <cstring>
#include <cassert>
#include <cerrno>

// Configuration constants
const size_t MAX_MSG_SIZE = 4096;
const size_t MAX_ARGS = 16;
const int SERVER_PORT = 3000;
const int POLL_TIMEOUT_MS = 1000;

// Response codes
enum ResponseCode
{
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2 // Key not exists
};

// Connection states
enum ConnectionState
{
    STATE_REQ = 0, // Waiting for or processing request
    STATE_RES = 1, // Sending response
    STATE_END = 2  // Connection ended
};

// Connection object to track client state
struct Connection
{
    int fd = -1;
    ConnectionState state = STATE_REQ;

    // Read buffer
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + MAX_MSG_SIZE];

    // Write buffer
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + MAX_MSG_SIZE];
};

// In-memory key-value store
static std::map<std::string, std::string> kv_store;

// Forward declarations
static void set_nonblocking(int fd);
static int32_t accept_connection(std::vector<Connection *> &connections, int server_fd);
static void handle_connection_io(Connection *conn);
static void cleanup_connections(std::vector<Connection *> &connections);
static int32_t process_request(const uint8_t *req, uint32_t req_len, uint32_t *resp_code, uint8_t *resp, uint32_t *resp_len);

// Utility function for error reporting
static void log_error(const char *message)
{
    perror(message);
}

// Set socket to non-blocking mode
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        log_error("fcntl F_GETFL error");
        return;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        log_error("fcntl F_SETFL error");
    }
}

// Add connection to connection list
static void add_connection(std::vector<Connection *> &connections, Connection *conn)
{
    if (connections.size() <= (size_t)conn->fd)
    {
        connections.resize(conn->fd + 1, nullptr);
    }
    connections[conn->fd] = conn;
}

// Parse client request into command parts
static int32_t parse_request(const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    // Need at least 4 bytes for argument count
    if (len < 4)
    {
        return -1;
    }

    // Get number of arguments
    uint32_t arg_count = 0;
    memcpy(&arg_count, &data[0], 4);
    if (arg_count > MAX_ARGS)
    {
        return -1;
    }

    // Parse each argument
    size_t pos = 4;
    while (arg_count--)
    {
        if (pos + 4 > len)
        {
            return -1; // Not enough data for string length
        }

        uint32_t str_len = 0;
        memcpy(&str_len, &data[pos], 4);

        if (pos + 4 + str_len > len)
        {
            return -1; // Not enough data for string content
        }

        out.push_back(std::string(reinterpret_cast<const char *>(&data[pos + 4]), str_len));
        pos += 4 + str_len;
    }

    // Ensure we consumed exactly all the data
    if (pos != len)
    {
        return -1;
    }

    return 0;
}

// Command handlers
static uint32_t handle_get(const std::vector<std::string> &cmd, uint8_t *resp, uint32_t *resp_len)
{
    const std::string &key = cmd[1];

    auto it = kv_store.find(key);
    if (it == kv_store.end())
    {
        return RES_NX; // Key not found
    }

    const std::string &value = it->second;
    if (value.size() > MAX_MSG_SIZE)
    {
        return RES_ERR; // Value too large (shouldn't happen, but safety check)
    }

    memcpy(resp, value.data(), value.size());
    *resp_len = static_cast<uint32_t>(value.size());
    return RES_OK;
}

static uint32_t handle_set(const std::vector<std::string> &cmd, uint8_t *resp, uint32_t *resp_len)
{
    const std::string &key = cmd[1];
    const std::string &value = cmd[2];

    kv_store[key] = value;

    *resp_len = 0; // No response data
    return RES_OK;
}

static uint32_t handle_del(const std::vector<std::string> &cmd, uint8_t *resp, uint32_t *resp_len)
{
    const std::string &key = cmd[1];

    kv_store.erase(key);

    *resp_len = 0; // No response data
    return RES_OK;
}
static bool try_process_request(Connection *conn);
// Try to receive data from client
static bool try_read_data(Connection *conn)
{
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    // Check if buffer has space
    size_t capacity = sizeof(conn->rbuf) - conn->rbuf_size;
    if (capacity == 0)
    {
        return false; // Buffer full
    }

    // Read data
    ssize_t bytes_read;
    do
    {
        bytes_read = read(conn->fd, &conn->rbuf[conn->rbuf_size], capacity);
    } while (bytes_read < 0 && errno == EINTR);

    // Handle read result
    if (bytes_read < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return false; // No data available
        }
        log_error("read() error");
        conn->state = STATE_END;
        return false;
    }

    if (bytes_read == 0)
    {
        // Client closed connection
        std::cout << "Client on fd " << conn->fd << " closed connection" << std::endl;
        conn->state = STATE_END;
        return false;
    }

    // Update buffer state
    conn->rbuf_size += static_cast<size_t>(bytes_read);

    // Process any complete requests
    while (try_process_request(conn))
    {
        // Keep processing while we have complete requests
    }

    return (conn->state == STATE_REQ);
}

static bool try_send_response(Connection *conn);
// Process a single request if we have a complete one
static bool try_process_request(Connection *conn)
{
    // Check if we have enough data for message length
    if (conn->rbuf_size < 4)
    {
        return false;
    }

    // Get message length
    uint32_t msg_len = 0;
    memcpy(&msg_len, &conn->rbuf[0], 4);
    msg_len = ntohl(msg_len); // Convert from network byte order

    // Validate message length
    if (msg_len > MAX_MSG_SIZE)
    {
        std::cout << "Message too large from fd " << conn->fd << ": "
                  << msg_len << " > " << MAX_MSG_SIZE << std::endl;
        conn->state = STATE_END;
        return false;
    }

    // Check if we have the complete message
    if (4 + msg_len > conn->rbuf_size)
    {
        return false; // Incomplete message
    }

    // Process the request
    uint32_t resp_code = 0;
    uint32_t resp_len = 0;
    int32_t result = process_request(
        &conn->rbuf[4], msg_len,
        &resp_code, &conn->wbuf[4 + 4], &resp_len);

    if (result != 0)
    {
        conn->state = STATE_END;
        return false;
    }

    // Prepare response
    uint32_t wlen = resp_len + 4; // Add 4 bytes for response code
    uint32_t wlen_network = htonl(wlen);
    uint32_t resp_code_network = htonl(resp_code);

    memcpy(&conn->wbuf[0], &wlen_network, 4);
    memcpy(&conn->wbuf[4], &resp_code_network, 4);
    conn->wbuf_size = 4 + wlen;
    conn->wbuf_sent = 0;

    // Remove processed request from buffer
    size_t remaining = conn->rbuf_size - 4 - msg_len;
    if (remaining > 0)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + msg_len], remaining);
    }
    conn->rbuf_size = remaining;

    // Switch to response mode
    conn->state = STATE_RES;
    try_send_response(conn);

    return (conn->state == STATE_REQ);
}

// Process the request and generate a response
static int32_t process_request(const uint8_t *req, uint32_t req_len,
                               uint32_t *resp_code, uint8_t *resp, uint32_t *resp_len)
{
    // Parse request into command parts
    std::vector<std::string> cmd;
    if (parse_request(req, req_len, cmd) != 0)
    {
        std::cout << "Bad request format" << std::endl;
        return -1;
    }

    // Execute command
    if (cmd.size() >= 1)
    {
        const std::string &command = cmd[0];

        if (cmd.size() == 2 && command == "get")
        {
            *resp_code = handle_get(cmd, resp, resp_len);
        }
        else if (cmd.size() == 3 && command == "set")
        {
            *resp_code = handle_set(cmd, resp, resp_len);
        }
        else if (cmd.size() == 2 && command == "del")
        {
            *resp_code = handle_del(cmd, resp, resp_len);
        }
        else
        {
            *resp_code = RES_ERR;
            const char *error_msg = "Unknown command";
            strcpy(reinterpret_cast<char *>(resp), error_msg);
            *resp_len = strlen(error_msg);
        }
    }
    else
    {
        *resp_code = RES_ERR;
        const char *error_msg = "Empty command";
        strcpy(reinterpret_cast<char *>(resp), error_msg);
        *resp_len = strlen(error_msg);
    }

    return 0;
}

// Try to send response data to client
static bool try_send_response(Connection *conn)
{
    while (true)
    {
        // Check if we have data to send
        size_t remaining = conn->wbuf_size - conn->wbuf_sent;
        if (remaining == 0)
        {
            // Response fully sent
            conn->state = STATE_REQ;
            conn->wbuf_sent = 0;
            conn->wbuf_size = 0;
            return false;
        }

        // Send data
        ssize_t bytes_sent;
        do
        {
            bytes_sent = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remaining);
        } while (bytes_sent < 0 && errno == EINTR);

        // Handle write result
        if (bytes_sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return false; // Would block
            }
            log_error("write() error");
            conn->state = STATE_END;
            return false;
        }

        conn->wbuf_sent += bytes_sent;

        // Check if response is complete
        if (conn->wbuf_sent == conn->wbuf_size)
        {
            conn->state = STATE_REQ;
            conn->wbuf_sent = 0;
            conn->wbuf_size = 0;
            return false;
        }
    }
}

// Handle connection I/O based on state
static void handle_connection_io(Connection *conn)
{
    if (!conn)
    {
        return;
    }

    if (conn->state == STATE_REQ)
    {
        while (try_read_data(conn))
        {
            // Keep reading while data is available
        }
    }
    else if (conn->state == STATE_RES)
    {
        while (try_send_response(conn))
        {
            // Keep sending while buffer can be flushed
        }
    }
}

// Accept new client connection
static int32_t accept_connection(std::vector<Connection *> &connections, int server_fd)
{
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);

    int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &socklen);
    if (client_fd < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }
        log_error("accept() error");
        return -1;
    }

    // Log connection
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    std::cout << "New connection from " << client_ip << ":"
              << ntohs(client_addr.sin_port) << ", fd=" << client_fd << std::endl;

    // Set non-blocking mode
    set_nonblocking(client_fd);

    // Create connection object
    Connection *conn = new Connection();
    conn->fd = client_fd;

    // Add to connection list
    add_connection(connections, conn);

    return 0;
}

// Cleanup closed connections
static void cleanup_connections(std::vector<Connection *> &connections)
{
    for (size_t i = 0; i < connections.size(); ++i)
    {
        Connection *conn = connections[i];
        if (!conn)
        {
            continue;
        }

        if (conn->state == STATE_END)
        {
            std::cout << "Cleaning up connection fd " << conn->fd << std::endl;
            close(conn->fd);
            connections[i] = nullptr;
            delete conn;
        }
    }
}

int main()
{
    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        log_error("socket() error");
        return 1;
    }

    // Set socket options
    int enable = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    {
        log_error("setsockopt() error");
        close(server_fd);
        return 1;
    }

    // Bind to address
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, reinterpret_cast<const sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
    {
        log_error("bind() error");
        close(server_fd);
        return 1;
    }

    // Listen for connections
    if (listen(server_fd, SOMAXCONN) < 0)
    {
        log_error("listen() error");
        close(server_fd);
        return 1;
    }

    std::cout << "Server listening on port " << SERVER_PORT << std::endl;

    // Set server socket to non-blocking mode
    set_nonblocking(server_fd);

    // Connection management
    std::vector<Connection *> connections;

    // Main event loop
    while (true)
    {
        // Clean up closed connections
        cleanup_connections(connections);

        // Prepare poll structures
        std::vector<struct pollfd> poll_fds;

        // Add server socket
        struct pollfd server_pollfd = {server_fd, POLLIN, 0};
        poll_fds.push_back(server_pollfd);

        // Add client connections
        for (size_t i = 0; i < connections.size(); ++i)
        {
            Connection *conn = connections[i];
            if (!conn)
            {
                continue;
            }

            struct pollfd client_pollfd = {};
            client_pollfd.fd = conn->fd;
            client_pollfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            poll_fds.push_back(client_pollfd);
        }

        // Wait for events
        int num_events = poll(poll_fds.data(), poll_fds.size(), POLL_TIMEOUT_MS);
        if (num_events < 0)
        {
            if (errno == EINTR)
            {
                continue; // Interrupted, try again
            }
            log_error("poll() error");
            break;
        }

        // Handle timeout - just continue
        if (num_events == 0)
        {
            continue;
        }

        // Process events
        for (size_t i = 0; i < poll_fds.size(); ++i)
        {
            if (poll_fds[i].revents == 0)
            {
                continue; // No events
            }

            int fd = poll_fds[i].fd;
            short revents = poll_fds[i].revents;

            if (fd == server_fd)
            {
                // New connection event
                if (revents & POLLIN)
                {
                    accept_connection(connections, server_fd);
                }
            }
            else
            {
                // Client activity
                if (fd >= 0 && fd < static_cast<int>(connections.size()) && connections[fd])
                {
                    handle_connection_io(connections[fd]);
                }
            }
        }
    }

    // Cleanup resources
    for (Connection *conn : connections)
    {
        if (conn)
        {
            close(conn->fd);
            delete conn;
        }
    }
    close(server_fd);

    return 0;
}