#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
constexpr const char* MESSAGE_TERMINATOR = ".";

static bool send_all(int fd, const void* buf, size_t len) {
  const char* p = static_cast<const char*>(buf);
  while (len > 0) {
    ssize_t n = send(fd, p, len, 0);
    if (n <= 0) return false;  // peer closed or error
    p += static_cast<size_t>(n);
    len -= static_cast<size_t>(n);
  }
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "Usage: " << argv[0] << " <ip> <port>\n";
    return 1;
  }

  const char* ip = argv[1];
  int port = std::stoi(argv[2]);
  std::cout << "Starting client -> " << ip << ":" << port << "\n";

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
    perror("inet_pton");
    return 1;
  }

  if (connect(sock, (sockaddr*)&sa, sizeof(sa)) < 0) {
    perror("connect");
    return 1;
  }

  std::string buffer, line;
  while (true) {
    if (!std::getline(std::cin, line)) break;
    if (line == MESSAGE_TERMINATOR) {
      // done collecting: send one length-prefixed block
      uint32_t nlen = htonl(static_cast<uint32_t>(buffer.size()));
      send_all(sock, &nlen, sizeof(nlen));
      send_all(sock, buffer.data(), buffer.size());
      buffer.clear();  // reset for next message
    } else {
      if (!buffer.empty()) buffer.push_back('\n');
      buffer += line;
    }
  }

  close(sock);
  return 0;
}
