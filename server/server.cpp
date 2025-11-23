#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>

#include "command_factory.h"  // neu
#include "commands.h"  // command_from, split_lines          :contentReference[oaicite:5]{index=5}
#include "network_utils.h"  // send_block / recv_block (hast du bereits) :contentReference[oaicite:4]{index=4}
#include "network_utils.h"  // send_block / recv_block (hast du bereits) :contentReference[oaicite:4]{index=4}
#include "network_utils.h"  // send_block / recv_block (hast du bereits) :contentReference[oaicite:4]{index=4}

static constexpr uint32_t MAX_PAYLOAD = 1u << 20;  // 1 MiB

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

    if (len == 0 ||
        len > MAX_PAYLOAD) {  // DoS prevention. send error if bigger 1MB
      (void)send_block(clientSocket, "ERR\n");
      close(clientSocket);
      return;
    }

    // 2) read payload
    std::string payload(len, '\0');
    if (len > 0 && !recv_exact(clientSocket, payload.data(), payload.size())) {
      std::cout << "Client disconnected.\n";
      close(clientSocket);
      return;
    }

    // 3) parse and dispatch
    auto lines = split_lines(payload);
    // lines ist bereits gefüllt, z.B. aus split_lines(payload)
    if (lines.empty()) {
      (void)send_block(clientSocket, "ERR\n");  // minimal
      continue;
    }

    CommandType type = command_from(lines[0]);
    auto cmd = CommandFactory::create(type);

    if (!cmd) {
      (void)send_block(clientSocket, "ERR\n");
      continue;
    }

    Context ctx{clientSocket, spoolDir};
    CommandOutcome out = cmd->execute(ctx, lines);

    // Antwort schicken (wenn vorhanden)
    if (!out.response.empty()) {
      (void)send_block(clientSocket, out.response);
    }

    // Verbindung schließen?
    if (out.shouldClose) {
      close(clientSocket);
      return;  // zurück zum accept() im Aufrufer
    }

    // Debug print of raw payload (optional; keep while developing)
    std::cout << "Client:\n" << payload << "\n";
  }
}

// ---------- signal handling ----------
static void reap_children(int) {
  while (waitpid(-1, nullptr, WNOHANG) > 0) {
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

  std::signal(SIGCHLD, reap_children);

  std::cout << "Server listening…\n";

  for (;;) {
    int clientSocket = accept(serverSocket, nullptr, nullptr);
    if (clientSocket < 0) {
      if (errno == EINTR) continue;
      perror("accept");
      continue;
    }

    std::cout << "Client connected.\n";

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      close(clientSocket);
      continue;
    }

    if (pid == 0) {
      // child
      close(serverSocket);
      handle_client(clientSocket, spoolDir);
      _exit(0);
    } else {
      // parent
      close(clientSocket);
    }
  }

  // Unreachable in this simple loop
  close(serverSocket);
  return 0;
}
