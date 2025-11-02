#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "network_utils.h"

constexpr const char* MESSAGE_TERMINATOR = ".";

// ---------- socket creation ----------
static int create_client_socket(const char* ip, int port) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return -1;
  }

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);

  if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
    perror("inet_pton");
    close(sock);
    return -1;
  }

  if (connect(sock, (sockaddr*)&sa, sizeof(sa)) < 0) {
    perror("connect");
    close(sock);
    return -1;
  }
  return sock;
}

// ---------- interactive input loop ----------
static void collect_and_send(int sock) {
  std::string buffer, line;

  while (true) {
    if (!std::getline(std::cin, line)) break;

    if (line == MESSAGE_TERMINATOR) {
      // finished one block: send it
      uint32_t nlen = htonl(static_cast<uint32_t>(buffer.size()));
      // message ([length: 4 bytes][data: length bytes]):
      if (!send_all(sock, &nlen, sizeof(nlen))) break;  // payload length
      if (!send_all(sock, buffer.data(), buffer.size())) break;  // payload

      // wait for server reply and print it
      std::string reply;
      if (!recv_block(sock, reply)) {
        std::cerr << "Disconnected while waiting for reply.\n";
        break;
      }
      std::cout << "Server:\n" << reply << std::flush;

      buffer.clear();
    } else {  // appends line to buffer with \n if not first line
      if (!buffer.empty()) buffer.push_back('\n');
      buffer += line;
    }
  }
}

// ---------- main ----------
int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "Usage: " << argv[0] << " <ip> <port>\n";
    return 1;
  }

  const char* ip = argv[1];
  int port = std::stoi(argv[2]);
  std::cout << "Starting client -> " << ip << ":" << port << "\n";

  int sock = create_client_socket(ip, port);
  if (sock < 0) return 1;

  collect_and_send(sock);

  close(sock);
  return 0;
}
