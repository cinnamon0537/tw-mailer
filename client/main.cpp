#include <iostream>
#include <getopt.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <ip> <port>\n";
        return 1;
    }

    std::string ip = argv[1];
    int port = std::stoi(argv[2]);

    std::cout << "Starting client at " << port << " for " << ip << "\n";

    return 0;
}