#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "commands.h"  // provides split_lines(), command_from(), CommandType

// ---------- low-level net helpers ----------
static bool recv_exact(int fd, void* buf, size_t len) {
  char* p = static_cast<char*>(buf);
  size_t got = 0;
  while (got < len) {
    ssize_t n = recv(fd, p + got, len - got, 0);
    if (n <= 0) return false;  // peer closed or error
    got += static_cast<size_t>(n);
  }
  return true;
}

static bool send_all(int fd, const void* buf, size_t len) {
  const char* p = static_cast<const char*>(buf);
  while (len > 0) {
    ssize_t n = send(fd, p, len, 0);
    if (n <= 0) return false;
    p += static_cast<size_t>(n);
    len -= static_cast<size_t>(n);
  }
  return true;
}

static bool send_block(int fd, const std::string& msg) {
  uint32_t n = htonl(static_cast<uint32_t>(msg.size()));
  return send_all(fd, &n, sizeof n) && send_all(fd, msg.data(), msg.size());
}

// ---------- server setup ----------
static int create_server_socket(int port) {
  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    perror("socket");
    return -1;
  }

  int yes = 1;
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(serverSocket);
    return -1;
  }
  if (listen(serverSocket, 5) < 0) {
    perror("listen");
    close(serverSocket);
    return -1;
  }
  return serverSocket;
}

// ---------- per-client handling (framing + dispatch) ----------
static void handle_client(int clientSocket, const std::string& spoolDir) {
  (void)spoolDir;  // will be used by handlers later

  for (;;) {
    // 1) read 4-byte length (network order)
    uint32_t nlen = 0;
    if (!recv_exact(clientSocket, &nlen, sizeof(nlen))) {
      std::cout << "Client disconnected.\n";
      close(clientSocket);
      return;
    }
    uint32_t len = ntohl(nlen);

    // 2) read payload
    std::string payload(len, '\0');
    if (len > 0 && !recv_exact(clientSocket, payload.data(), payload.size())) {
      std::cout << "Client disconnected.\n";
      close(clientSocket);
      return;
    }

    // 3) parse and dispatch
    auto lines = split_lines(payload);
    if (lines.empty()) {
      (void)send_block(clientSocket, "ERR\n");
      continue;
    }

    CommandType cmd = command_from(lines[0]);
    switch (cmd) {
      case CommandType::SEND:
        std::cout << "SEND command\n";
        // TODO: implement send; for now acknowledge
        (void)send_block(clientSocket, "OK\n");
        break;

      case CommandType::LIST:
        std::cout << "LIST command\n";
        // TODO: implement listing; placeholder
        (void)send_block(clientSocket, "0\n");  // e.g., 0 messages for now
        break;

      case CommandType::READ:
        std::cout << "READ command\n";
        // TODO: implement read; placeholder error
        (void)send_block(clientSocket, "ERR\n");
        break;

      case CommandType::DEL:
        std::cout << "DEL command\n";
        // TODO: implement delete; placeholder
        (void)send_block(clientSocket, "OK\n");
        break;

      case CommandType::QUIT:
        std::cout << "QUIT command\n";
        // per spec: no response
        close(clientSocket);
        return;

      default:
        std::cout << "Unknown command\n";
        (void)send_block(clientSocket, "ERR\n");
        break;
    }

    // Debug print of raw payload (optional; keep while developing)
    std::cout << "Client:\n" << payload << "\n";
  }
}

// ---------- main: thin orchestration ----------
int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "Usage: " << argv[0] << " <port> <mail-spool-directoryname>\n";
    return 1;
  }

  int port = std::stoi(argv[1]);
  std::string spoolDir = argv[2];
  std::cout << "Starting server on port " << port << " (spool dir: " << spoolDir
            << ")\n";

  int serverSocket = create_server_socket(port);
  if (serverSocket < 0) return 1;

  std::cout << "Server listening…\n";

  for (;;) {
    int clientSocket = accept(serverSocket, nullptr, nullptr);
    if (clientSocket < 0) {
      perror("accept");
      continue;
    }

    std::cout << "Client connected.\n";
    handle_client(clientSocket, spoolDir);
    // loop back to accept() for the next client
  }

  // Unreachable in this simple loop
  close(serverSocket);
  return 0;
}
