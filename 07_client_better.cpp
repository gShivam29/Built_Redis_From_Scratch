#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const size_t MAX_MSG_SIZE = 4096;

class SocketClient
{
private:
    int fd_;

    // Read exactly n bytes from socket
    bool readExactly(char *buf, size_t n)
    {
        size_t bytesRead = 0;
        while (bytesRead < n)
        {
            ssize_t result = read(fd_, buf + bytesRead, n - bytesRead);
            if (result <= 0)
            {
                std::cerr << "Read error: " << strerror(errno) << std::endl;
                return false;
            }
            bytesRead += result;
        }
        return true;
    }

    // Write exactly n bytes to socket
    bool writeExactly(const char *buf, size_t n)
    {
        size_t bytesWritten = 0;
        while (bytesWritten < n)
        {
            ssize_t result = write(fd_, buf + bytesWritten, n - bytesWritten);
            if (result <= 0)
            {
                std::cerr << "Write error: " << strerror(errno) << std::endl;
                return false;
            }
            bytesWritten += result;
        }
        return true;
    }

public:
    SocketClient() : fd_(-1) {}

    ~SocketClient()
    {
        disconnect();
    }

    bool connect(const std::string &host = "127.0.0.1", uint16_t port = 3000)
    {
        // Create socket
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
        {
            std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
            return false;
        }

        // Setup server address
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (host == "localhost" || host == "127.0.0.1")
        {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        else
        {
            inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        }

        // Connect to server
        if (::connect(fd_, (const struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            std::cerr << "Connection failed: " << strerror(errno) << std::endl;
            close(fd_);
            fd_ = -1;
            return false;
        }

        return true;
    }

    void disconnect()
    {
        if (fd_ >= 0)
        {
            close(fd_);
            fd_ = -1;
        }
    }

    bool isConnected() const
    {
        return fd_ >= 0;
    }

    // Send a simple string message
    bool sendMessage(const std::string &message)
    {
        if (!isConnected())
            return false;

        if (message.length() > MAX_MSG_SIZE)
        {
            std::cerr << "Message too long" << std::endl;
            return false;
        }

        // Format: [length:4][message:length]
        uint32_t length = htonl(message.length());

        char buffer[4 + MAX_MSG_SIZE];
        memcpy(buffer, &length, 4);
        memcpy(buffer + 4, message.c_str(), message.length());

        return writeExactly(buffer, 4 + message.length());
    }

    // Send a command (vector of strings)
    bool sendCommand(const std::vector<std::string> &cmd)
    {
        if (!isConnected())
            return false;

        // Calculate total length
        uint32_t dataLength = 4; // For cmd.size()
        for (const auto &s : cmd)
        {
            dataLength += 4 + s.size(); // 4 bytes for length + string content
        }

        if (dataLength > MAX_MSG_SIZE)
        {
            std::cerr << "Command too long" << std::endl;
            return false;
        }

        // Format: [total_length:4][num_args:4][arg1_len:4][arg1:len]...
        char buffer[4 + MAX_MSG_SIZE];

        // First 4 bytes: total length
        uint32_t netLength = htonl(dataLength);
        memcpy(buffer, &netLength, 4);

        // Next 4 bytes: number of arguments
        uint32_t numArgs = htonl(cmd.size());
        memcpy(buffer + 4, &numArgs, 4);

        // Add each command argument
        size_t offset = 8;
        for (const auto &s : cmd)
        {
            uint32_t strLen = htonl(s.size());
            memcpy(buffer + offset, &strLen, 4);
            memcpy(buffer + offset + 4, s.c_str(), s.size());
            offset += 4 + s.size();
        }

        return writeExactly(buffer, 4 + dataLength);
    }

    // Read a response from the server
    bool readResponse(std::string &response)
    {
        if (!isConnected())
            return false;

        char buffer[4 + MAX_MSG_SIZE + 1];

        // Read message length (first 4 bytes)
        if (!readExactly(buffer, 4))
        {
            return false;
        }

        // Extract message length
        uint32_t length;
        memcpy(&length, buffer, 4);
        length = ntohl(length);

        if (length > MAX_MSG_SIZE)
        {
            std::cerr << "Response too long" << std::endl;
            return false;
        }

        // Read the actual message content
        if (!readExactly(buffer + 4, length))
        {
            return false;
        }

        // Parse the response code and message
        if (length < 4)
        {
            std::cerr << "Bad response: too short" << std::endl;
            return false;
        }

        // Extract response code
        uint32_t responseCode;
        memcpy(&responseCode, buffer + 4, 4);

        // Set null terminator for the message part
        buffer[4 + length] = '\0';

        // Display the response
        std::cout << "Server response: [" << responseCode << "] "
                  << std::string(buffer + 8, length - 4) << std::endl;

        // Store the full response
        response = std::string(buffer + 4, length);

        return true;
    }
};

int main(int argc, char **argv)
{
    SocketClient client;

    // Connect to server
    if (!client.connect())
    {
        return 1;
    }

    // Process command line arguments if provided
    // if (argc > 1)
    // {
    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i)
    {
        cmd.push_back(argv[i]);
    }

    if (!client.sendCommand(cmd))
    {
        return 1;
    }

    std::string response;
    if (!client.readResponse(response))
    {
        return 1;
    }
    // }
    // else
    // {
    //     // Default test messages if no arguments provided
    //     const std::vector<std::string> testMessages = {"hello1", "hello2", "hello3"};

    //     for (const auto &msg : testMessages)
    //     {
    //         std::cout << "Sending message: " << msg << std::endl;

    //         if (!client.sendMessage(msg))
    //         {
    //             return 1;
    //         }

    //         std::string response;
    //         if (!client.readResponse(response))
    //         {
    //             return 1;
    //         }
    //     }
    // }

    return 0;
}