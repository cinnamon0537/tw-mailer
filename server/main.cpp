#include <iostream>
#include <getopt.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <port> <mail-spool-directoryname>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string msg = argv[2];

    std::cout << "Starting server at " << port << " for " << msg << "\n";

    return 0;
}