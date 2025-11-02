#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <string>

#include "network_utils.h"

constexpr const char* MESSAGE_TERMINATOR = ".";
static constexpr uint32_t MAX_PAYLOAD = 1u << 20; // 1 MiB

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

  // ---- per-socket timeouts (10s read/write) ----
  timeval tv{ .tv_sec = 10, .tv_usec = 0 };
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("setsockopt(SO_RCVTIMEO)");
  }
  if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
    perror("setsockopt(SO_SNDTIMEO)");
  }

  return sock;
}

// ---------- interactive input loop ----------
static void collect_and_send(int sock) {
  std::string buffer, line;

  while (true) {
    if (!std::getline(std::cin, line)) break;

    if (line == MESSAGE_TERMINATOR) {
      // finished one block: validate and send it
      if (buffer.empty()) {
        std::cerr << "Message is empty; not sending. Type lines, then '.'\n";
        continue;
      }
      if (buffer.size() > MAX_PAYLOAD) {
        std::cerr << "Message too large (" << buffer.size()
                  << " bytes). Max is " << MAX_PAYLOAD << " bytes.\n";
        buffer.clear();
        continue;
      }

      uint32_t nlen = htonl(static_cast<uint32_t>(buffer.size()));
      // message ([length: 4 bytes][data: length bytes]):
      if (!send_all(sock, &nlen, sizeof(nlen))) {
        std::cerr << "Send failed (length). errno=" << errno << "\n";
        break;
      }
      if (!send_all(sock, buffer.data(), buffer.size())) {
        std::cerr << "Send failed (payload). errno=" << errno << "\n";
        break;
      }

      // wait for server reply and print it
      std::string reply;
      if (!recv_block(sock, reply)) {
        std::cerr << "Disconnected or timeout while waiting for reply.\n";
        break;
      }
      std::cout << "Server:\n" << reply << std::flush;

      buffer.clear();
    } else {  // append line to buffer (with newline if not first line)
      if (!buffer.empty()) buffer.push_back('\n');
      buffer += line;

      // Optional live guard to avoid building huge buffers interactively
      if (buffer.size() > MAX_PAYLOAD) {
        std::cerr << "Buffer exceeded " << MAX_PAYLOAD
                  << " bytes. Truncating and waiting for terminator '.'\n";
        buffer.resize(MAX_PAYLOAD);
      }
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
