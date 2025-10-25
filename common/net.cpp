#include "network_utils.h"

bool send_all(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return false;  // peer closed or error
        p += static_cast<size_t>(n);
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool recv_exact(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

bool recv_block(int fd, std::string& out) {
    uint32_t n = 0;
    if (!recv_exact(fd, &n, sizeof n)) return false;
    uint32_t len = ntohl(n);
    out.assign(len, '\0');
    if (len == 0) return true;
    return recv_exact(fd, out.data(), out.size());
}

bool send_block(int fd, const std::string& msg) {
  uint32_t n = htonl(static_cast<uint32_t>(msg.size()));
  return send_all(fd, &n, sizeof n) && send_all(fd, msg.data(), msg.size());
}