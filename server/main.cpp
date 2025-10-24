#include <iostream>
#include <string>
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static bool recv_exact(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) return false; // peer closed or error
        got += static_cast<size_t>(n);
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <port> <mail-spool-directoryname>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string spoolDir = argv[2];
    std::cout << "Starting server on port " << port
              << " (spool dir: " << spoolDir << ")\n";

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(serverSocket, 5) < 0) { perror("listen"); return 1; }

    std::cout << "Server listening…\n";

    for (;;) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket < 0) { perror("accept"); continue; }

        std::cout << "Client connected.\n";
        for (;;) {
            // read 4-byte length (network order)
            uint32_t nlen = 0;
            if (!recv_exact(clientSocket, &nlen, sizeof(nlen))) {
                std::cout << "Client disconnected.\n";
                close(clientSocket);
                break; // back to accept() for next client
            }
            uint32_t len = ntohl(nlen);

            // read payload
            std::string payload(len, '\0');
            if (len > 0 && !recv_exact(clientSocket, payload.data(), payload.size())) {
                std::cout << "Client disconnected.\n";
                close(clientSocket);
                break;
            }

            // For now: just print what we got (you'll hook protocol here)
            std::cout << "Client: " << payload << "\n";
        }
    }

    // (Unreachable in this simple loop; add a shutdown path if you need one)
    close(serverSocket);
    return 0;
}
