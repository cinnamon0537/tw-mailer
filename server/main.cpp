#include <iostream>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " <port> <mail-spool-directoryname>" << std::endl;
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string msg = argv[2];

    std::cout << "Starting server at " << port << " for " << msg << std::endl;

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));

    listen(serverSocket, 5);

    int clientSocket = accept(serverSocket, nullptr, nullptr);

    uint32_t length;
    for (;;)
    {
        recv(clientSocket, &length, sizeof(length), 0);
        std::string buffer(length, '\0');
        recv(clientSocket, &buffer[0], length, 0);
        std::cout << "Client: " << buffer << std::endl;
    }
    close(serverSocket);

    return 0;
}