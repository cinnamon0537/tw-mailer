#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

constexpr const char* MESSAGE_TERMINATOR = ".";

// ---------- low-level net helpers ----------
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

static bool recv_exact(int fd, void* buf, size_t len) {
  char* p = static_cast<char*>(buf);
  size_t got = 0;
  while (got < len) {
    ssize_t n = recv(fd, p + got, len - got, 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

static bool recv_block(int fd, std::string& out) {
  uint32_t n = 0;
  if (!recv_exact(fd, &n, sizeof n)) return false;
  uint32_t len = ntohl(n);
  out.assign(len, '\0');
  if (len == 0) return true;
  return recv_exact(fd, out.data(), out.size());
}

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
      if (!send_all(sock, &nlen, sizeof(nlen))) break;
      if (!send_all(sock, buffer.data(), buffer.size())) break;

      // wait for server reply and print it
      std::string reply;
      if (!recv_block(sock, reply)) {
        std::cerr << "Disconnected while waiting for reply.\n";
        break;
      }
      std::cout << "Server:\n" << reply << std::flush;

      buffer.clear();
    } else {
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
