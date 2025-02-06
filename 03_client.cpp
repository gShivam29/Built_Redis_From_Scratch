#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

void die(const char* message){
    perror(message);
    exit(1);
}

int main(){

    int fd = socket(AF_INET, SOCK_STREAM, 0); //create file descriptor using socket
    if (fd<0){
        die("socket()");
    }

    struct sockaddr_in addr = {}; //struct from the netinet file
    addr.sin_family = AF_INET;// ip belongs to ipv4 hai iska mtlb
    addr.sin_port = ntohs(3000);//port
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); //INADDR_LOOPBACK is a constant for localhost 127.0.0.1 in networking
    int rv = connect(fd,(const struct sockaddr*)&addr, sizeof(addr));

    if (rv){
        die("connect");
    }

    char msg[] = "Shivam says hello ";
    write(fd,msg,strlen(msg));

    char rbuf[64] = {};

    ssize_t n = read(fd,rbuf,sizeof(rbuf)-1);

    if (n<0) die("read");

    printf("Server says: %s\n",rbuf);
    close(fd);
    return 0;
}