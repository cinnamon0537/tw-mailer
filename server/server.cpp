#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <csignal>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "auth_manager.h"
#include "command_factory.h"
#include "commands.h"       // command_from, split_lines
#include "network_utils.h"  // send_block / recv_block

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

// ---------- helper: get client IP address ----------
static std::string get_client_ip(int clientSocket) {
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (getpeername(clientSocket, (sockaddr*)&addr, &len) == 0) {
    char ipstr[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ipstr, sizeof(ipstr))) {
      return std::string(ipstr);
    }
  }
  return "unknown";
}

// ---------- per-client handling (framing + dispatch) ----------
static void handle_client(int clientSocket, const std::string& spoolDir) {
  // Get client IP address
  std::string clientIP = get_client_ip(clientSocket);

  // Create context once per client session to maintain authentication state
  Context ctx{clientSocket, spoolDir, "",
              clientIP};  // Start with empty authenticatedUser

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

    // Execute command with persistent context (authentication state is
    // maintained)
    CommandOutcome out = cmd->execute(ctx, lines);

    // Antwort schicken (wenn vorhanden)
    if (!out.response.empty()) {
      (void)send_block(clientSocket, out.response);
    }

    // Verbindung schließen?
    if (out.shouldClose) {
      close(clientSocket);
      return;  // back to caller (child exits)
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

  // Initialize authentication manager with blacklist file
  std::string blacklistFile = spoolDir + "/.blacklist";
  AuthManager authManager(blacklistFile);
  setAuthManager(&authManager);

  int serverSocket = create_server_socket(port);
  if (serverSocket < 0) return 1;

  std::cout << "Server listening…\n";

  // Automatically reap finished children (no zombies)
  std::signal(SIGCHLD, SIG_IGN);

  for (;;) {
    int clientSocket = accept(serverSocket, nullptr, nullptr);
    if (clientSocket < 0) {
      perror("accept");
      continue;
    }

    std::cout << "Client connected.\n";

    pid_t pid = fork();
    if (pid < 0) {
      // fork failed – handle client in this process as fallback
      perror("fork");
      handle_client(clientSocket, spoolDir);
      // handle_client is responsible for closing the socket
      continue;
    }

    if (pid == 0) {
      // --- Child process: handle this one client ---
      close(serverSocket);  // child doesn't accept new clients
      handle_client(clientSocket, spoolDir);
      // socket closed inside handle_client or by process exit
      _exit(0);
    } else {
      // --- Parent process: go back to accept() ---
      close(clientSocket);  // parent doesn't talk to this client
    }
  }

  // Unreachable in this simple loop
  close(serverSocket);
  return 0;
}
