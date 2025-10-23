#include <iostream>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " <ip> <port>\n";
        return 1;
    }

    std::string ip = argv[1];
    int port = std::stoi(argv[2]);

    std::cout << "Starting client at " << port << " for " << ip << "\n";

    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    serverAddress.sin_addr.s_addr = inet_addr(ip.c_str());

    connect(clientSocket, (struct sockaddr *)&serverAddress,
            sizeof(serverAddress));

    std::string input;
    for (;;)
    {
        std::getline(std::cin, input);
        send(clientSocket, input.c_str(), input.length(), 0);
    }

    close(clientSocket);

    return 0;
}