#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <cstdlib>

const size_t k_max_msg = 4096;

void die(const char *message)
{
    perror(message);
    exit(1);
}

void msg(const char *message)
{
    std::cout << message << std::endl;
}

static int32_t read_full(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n);

        if (rv <= 0)
            return -1; // Error or connection closed

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(fd, buf, n);

        if (rv <= 0)
            return -1;

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t send_req(int fd, const char *text)
{
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg)
    {
        msg("Message too long");
        return -1;
    }

    char wbuf[4 + k_max_msg];
    uint32_t len_n = htonl(len); // Convert to network byte order
    memcpy(wbuf, &len_n, 4);
    memcpy(&wbuf[4], text, len);

    if (int32_t err = write_all(fd, wbuf, 4 + len))
    {
        msg("Failed to send message");
        return err;
    }
    return 0;
}

static int32_t read_res(int fd)
{
    char rbuf[4 + k_max_msg + 1];
    errno = 0;

    // Read message length (4 bytes)
    int32_t err = read_full(fd, rbuf, 4);
    if (err)
    {
        if (errno == 0)
        {
            msg("EOF");
        }
        else
        {
            msg("read() error");
        }
        return err;
    }

    // Convert from network byte order
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    len = ntohl(len);

    if (len > k_max_msg)
    {
        msg("Response too long");
        return -1;
    }

    // Read the actual message
    err = read_full(fd, &rbuf[4], len);
    if (err)
    {
        msg("read() error");
        return err;
    }

    rbuf[4 + len] = '\0';
    printf("server says: %s\n", &rbuf[4]);
    return 0;
}

int main()
{
    // Create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket()");
    }

    // Setup server address
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    // Connect to server
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv)
    {
        die("connect");
    }

    // Send messages and receive responses
    const char *query_list[3] = {"hello1", "hello2", "hello3"};
    bool success = true;

    // Send all messages first
    for (size_t i = 0; i < 3; ++i)
    {
        std::cout << "Sending message: " << query_list[i] << std::endl;
        int32_t err = send_req(fd, query_list[i]);
        if (err)
        {
            success = false;
            break;
        }
    }

    // Then read all responses
    if (success)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            int32_t err = read_res(fd);
            if (err)
            {
                success = false;
                break;
            }
        }
    }

    // Cleanup
    close(fd);
    return success ? 0 : 1;
}