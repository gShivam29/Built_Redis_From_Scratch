#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <cassert>

const size_t k_max_msg = 4096;

void die(const char *message)
{
    perror(message);
    exit(1);
}

void msg(char *message)
{
    std::cout << message << std::endl;
}

static int32_t read_full(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n); // reads from fd to buf and only reads n bytes

        if (rv <= 0)
            return -1; // checks for erros

        assert((size_t)rv <= n); // sanity check, if read value is bigger than n then it is a no no

        n -= (size_t)rv; // updates n if n is partially read

        buf += rv; // moves the pointer by the bytes read so that it points to a new memory block so that new values could be inserterd
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(fd, buf, n);

        if (rv <= 0)
        {
            return -1;
        }

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t query(int fd, const char *text)
{
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg)
    {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    if (int32_t err = write_all(fd, wbuf, 4 + len))
    {
        return err;
    }

    char rbuf[4 + k_max_msg + 1];
    errno = 0;
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

    memcpy(&len, rbuf, 4);
    if (len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

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

    int fd = socket(AF_INET, SOCK_STREAM, 0); // create file descriptor using socket
    if (fd < 0)
    {
        die("socket()");
    }

    struct sockaddr_in addr = {};                  // struct from the netinet file
    addr.sin_family = AF_INET;                     // ip belongs to ipv4 hai iska mtlb
    addr.sin_port = ntohs(3000);                   // port
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // INADDR_LOOPBACK is a constant for localhost 127.0.0.1 in networking
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("connect");
    }

    // char msg[] = "shivam";
    // write(fd, msg, strlen(msg));

    // char rbuf[64] = {};

    // ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);

    // if (n < 0)
    //     die("read");

    // printf("Server says: %s\n",rbuf);
    int32_t err = query(fd, "hello1");
    if (err)
    {
        goto L_DONE;
    }

    err = query(fd, "hello2");
    if (err)
    {
        goto L_DONE;
    }

    err = query(fd, "hello3");
    if (err)
    {
        goto L_DONE;
    }

L_DONE:
    close(fd);
    return 0;
}