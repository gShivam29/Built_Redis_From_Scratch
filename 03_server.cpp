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

static int32_t one_request(int connfd)
{
    char rbuf[4 + k_max_msg + 1];

    errno = 0;

    int32_t err = read_full(connfd, rbuf, 4);

    if (err)
    {
        if (errno == 0)
        {
            msg("EOF");
            return -1;
        }
        else
        {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;

    memcpy(&len, rbuf, 4);

    if (len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    err = read_full(connfd, &rbuf[4], len);
    if (err)
    {
        msg("read() error");
        return err;
    }

    rbuf[4 + len] = '\0';
    printf("Client says: %s\n", &rbuf[4]);

    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);

    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

static void do_something(int connfd)
{
    char rbuf[64] = {};                               // read buffer
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1); // read function populates the rbuf, -1 from sizeof(rbuf) to store \0 character to end, connfd is the connection with client
    if (n < 0)
    {
        die("read() error");

        return;
    }

    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf)); // returns value to client shows up in the client console
}

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0); // returns a file descriptor

    if (fd == -1) // if socket function fails it returns -1
    {
        std::cout << "Error creating a socket" << std::endl;
        return -1;
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

    while (true)
    {
        struct sockaddr_in client_addr = {}; // sockaddr_in stores ips, {} sets all the members to 0

        socklen_t socklen = sizeof(client_addr); // socklen_t is an unsigned integer type datatype used to rep the size in bytes of socket addresses.

        int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);

        /* fd is the original fd created using socket and bound using bind
        the sockaddr pointer points to an area that stores the address of the client it is mandatory for the accept function
        &socklen is A pointer to the variable that holds the size of client_addr */

        if (connfd < 0)
        {
            continue;
        }

        // do_something(connfd);

        while (true)
        {
            int32_t err = one_request(connfd);
            if (err == -1) break;
        }

        close(connfd);
    }

    return 0;
}