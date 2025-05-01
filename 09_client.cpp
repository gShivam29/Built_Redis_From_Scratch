#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <cstdlib>
#include <vector>
const size_t k_max_msg = 4096;

enum
{
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
};

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

static int32_t on_response(const uint8_t *data, size_t size)
{
    if (size < 1)
    {
        msg("bad response1");
        return -1;
    }

    switch (data[0])
    {
    case SER_NIL:
        printf("(nil)\n");
        return 1;
    case SER_ERR:
        if (size < 1 + 8)
        {
            msg("bad response2");
            return -1;
        }
        else
        {
            int32_t code = 0;
            uint32_t len = 0;
            memcpy(&code, &data[1], 4);
            memcpy(&len, &data[1 + 4], 4);
            if (size < 1 + 8 + len)
            {
                msg("bad response3");
                return -1;
            }
            printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
            return 1 + 8 + len;
        }

    case SER_STR:
        if (size < 1 + 4)
        {
            msg("bad response4");
            return -1;
        }
        else
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);

            if (size < 1 + 4 + len)
            {
                msg("bad response5");
                return -1; // Ensure the response includes the string data
            }

            std::string out;
            out.append((char *)&data[5], len); // Start after 1 byte for SER_STR and 4 bytes for the length

            // Uniform printf statement
            printf("(str) %.*s\n", len, out.c_str());
            return 1 + 4 + len; // Return total processed bytes: 1 byte for SER_STR + 4 bytes for length + 'len' bytes for the string
        }
    case SER_INT:
        if (size < 1 + 8)
        {
            msg("bad response6");
            return -1;
        }
        else
        {
            int64_t val = 0;
            memcpy(&val, &data[1], 8); // Deserialize 8 bytes into int64_t

            // Uniform printf statement
            std::cout << "(int) " << val << std::endl; // Print the integer value (long long for int64_t)
            return 1 + 8;                              // Return the total number of bytes read (1 byte for SER_INT + 8 bytes for the int64_t value)
        }

    case SER_ARR:
        if (size < 1 + 4)
        {
            msg("bad response7");
            return -1;
        }
        else
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            printf("(arr) len=%u\n", len);
            size_t arr_bytes = 1 + 4;
            for (uint32_t i = 0; i < len; i++)
            {
                int32_t rv = on_response(&data[arr_bytes], size - arr_bytes);
                if (rv < 0)
                {
                    return rv;
                }
                arr_bytes += (size_t)rv;
            }
            printf("(arr) end\n");
            return (int32_t)arr_bytes;
        }
    default:
        msg("bad response8");
        return -1;
    }
}
// static int32_t read_res(int fd)
// {
//     char rbuf[4 + k_max_msg + 1];
//     errno = 0;

//     // Read message length (4 bytes)
//     int32_t err = read_full(fd, rbuf, 4);
//     if (err)
//     {
//         if (errno == 0)
//         {
//             msg("EOF");
//         }
//         else
//         {
//             msg("read() error");
//         }
//         return err;
//     }

//     // Convert from network byte order
//     uint32_t len = 0;
//     memcpy(&len, rbuf, 4);
//     // len = ntohl(len);

//     if (len > k_max_msg)
//     {
//         msg("Response too long");
//         return -1;
//     }

//     // Read the actual message
//     err = read_full(fd, &rbuf[4], len);
//     if (err)
//     {
//         msg("read() error");
//         return err;
//     }

//     rbuf[4 + len] = '\0';
//     printf("server says: %s\n", &rbuf[4]);

//     uint32_t rescode = 0;
//     if (len < 4)
//     {
//         msg("bad response");
//         return -1;
//     }
//     memcpy(&rescode, &rbuf[4], 4);
//     printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
//     return 0;
// }

static int32_t read_res(int fd)
{
    // First read the message length (4 bytes)
    char lenbuf[4];
    errno = 0;

    int32_t err = read_full(fd, lenbuf, 4);
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
    memcpy(&len, lenbuf, 4);

    if (len > k_max_msg)
    {
        msg("Response too long");
        return -1;
    }

    // Now read the actual payload
    char rbuf[k_max_msg];
    err = read_full(fd, rbuf, len);
    if (err)
    {
        msg("read() error");
        return err;
    }

    // Process the response - passing only the payload (not the length prefix)
    return on_response(reinterpret_cast<const uint8_t *>(rbuf), len);
}

int32_t send_req(int fd, const std::vector<std::string> &cmd)
{
    uint32_t len = 4;
    for (const std::string &s : cmd)
    {
        len += 4 + s.size();
    }
    if (len > k_max_msg)
    {
        msg("Request too long");
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &len, 4);

    uint32_t n = cmd.size();

    memcpy(&wbuf[4], &n, 4);

    size_t curr = 8;

    for (const std::string &s : cmd)
    {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[curr], &p, 4);
        memcpy(&wbuf[curr + 4], s.data(), s.size());
        curr += 4 + s.size();
    }
    return write_all(fd, wbuf, 4 + len);
}

int main(int argc, char **argv)
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

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i)
    {
        cmd.push_back(argv[i]);
    }
    int32_t err = send_req(fd, cmd);
    if (err)
    {
        printf("err");
        close(fd);
        return 0;
    }
    err = read_res(fd);
    if (err)
    {
        close(fd);
        return 0;
    }
    // Cleanup
    close(fd);
    return 0;
    // return success ? 0 : 1;
}