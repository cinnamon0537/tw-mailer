#include "auth_manager.h"

#include <ldap.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

// LDAP server configuration
static constexpr const char* LDAP_HOST = "ldap.technikum-wien.at";
static constexpr int LDAP_SERVER_PORT = 389;
static constexpr const char* LDAP_BASE = "dc=technikum-wien,dc=at";

AuthManager::AuthManager(const std::string& blacklistFile)
    : blacklistFile_(blacklistFile),
      attemptsFile_(blacklistFile + ".attempts") {
  // Files will be loaded on-demand with proper locking
}

AuthManager::~AuthManager() {
  // No need to save on destruction - each operation saves immediately
}

int AuthManager::acquireLock(const std::string& lockfile) {
  int fd = open(lockfile.c_str(), O_CREAT | O_RDWR, 0644);
  if (fd < 0) return -1;

  if (flock(fd, LOCK_EX) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

void AuthManager::releaseLock(int fd) {
  if (fd >= 0) {
    flock(fd, LOCK_UN);
    close(fd);
  }
}

std::string AuthManager::makeKey(const std::string& ip,
                                 const std::string& username) {
  return ip + ":" + username;
}

bool AuthManager::isBlacklisted(const std::string& ip) {
  std::string lockfile = blacklistFile_ + ".lock";
  int lockfd = acquireLock(lockfile);
  if (lockfd < 0) return false;  // Can't lock, assume not blacklisted

  std::map<std::string, BlacklistEntry> blacklist;
  loadBlacklist(blacklist);

  std::time_t now = std::time(nullptr);
  auto it = blacklist.find(ip);
  bool result = false;

  if (it != blacklist.end()) {
    if (now < it->second.blockedUntil) {
      result = true;  // Still blacklisted
    } else {
      // Expired, remove it
      blacklist.erase(it);
      saveBlacklist(blacklist);
    }
  }

  releaseLock(lockfd);
  return result;
}

bool AuthManager::recordFailedAttempt(const std::string& ip,
                                      const std::string& username) {
  std::string lockfile = blacklistFile_ + ".lock";
  int lockfd = acquireLock(lockfile);
  if (lockfd < 0) return false;

  std::map<std::string, AttemptInfo> attempts;
  std::map<std::string, BlacklistEntry> blacklist;

  loadAttempts(attempts);
  loadBlacklist(blacklist);

  std::string key = makeKey(ip, username);
  std::time_t now = std::time(nullptr);

  auto& info = attempts[key];
  info.count++;
  info.lastAttempt = now;

  bool shouldBlacklist = false;
  if (info.count >= MAX_ATTEMPTS) {
    // Blacklist this IP
    BlacklistEntry entry;
    entry.blockedUntil = now + BLACKLIST_DURATION_SECONDS;
    blacklist[ip] = entry;
    shouldBlacklist = true;

    // Clear attempts for this IP:username
    attempts.erase(key);
  }

  cleanupExpiredEntries(blacklist, attempts);
  saveAttempts(attempts);
  saveBlacklist(blacklist);

  releaseLock(lockfd);
  return shouldBlacklist;
}

void AuthManager::recordSuccess(const std::string& ip,
                                const std::string& username) {
  std::string lockfile = blacklistFile_ + ".lock";
  int lockfd = acquireLock(lockfile);
  if (lockfd < 0) return;

  std::map<std::string, AttemptInfo> attempts;
  loadAttempts(attempts);

  std::string key = makeKey(ip, username);
  attempts.erase(key);

  saveAttempts(attempts);
  releaseLock(lockfd);
}

void AuthManager::cleanupExpiredEntries(
    std::map<std::string, BlacklistEntry>& blacklist,
    std::map<std::string, AttemptInfo>& attempts) {
  std::time_t now = std::time(nullptr);

  // Remove expired blacklist entries
  for (auto it = blacklist.begin(); it != blacklist.end();) {
    if (now >= it->second.blockedUntil) {
      it = blacklist.erase(it);
    } else {
      ++it;
    }
  }

  // Clean up old attempt records (older than blacklist duration)
  std::time_t cutoff = now - BLACKLIST_DURATION_SECONDS;
  for (auto it = attempts.begin(); it != attempts.end();) {
    if (it->second.lastAttempt < cutoff) {
      it = attempts.erase(it);
    } else {
      ++it;
    }
  }
}

void AuthManager::loadBlacklist(
    std::map<std::string, BlacklistEntry>& blacklist) {
  std::ifstream file(blacklistFile_);
  if (!file.is_open()) {
    return;  // File doesn't exist yet, that's okay
  }

  std::string line;
  std::time_t now = std::time(nullptr);

  while (std::getline(file, line)) {
    if (line.empty()) continue;

    std::istringstream iss(line);
    std::string ip;
    std::time_t blockedUntil;

    if (iss >> ip >> blockedUntil) {
      // Only load if not expired
      if (blockedUntil > now) {
        BlacklistEntry entry;
        entry.blockedUntil = blockedUntil;
        blacklist[ip] = entry;
      }
    }
  }
}

void AuthManager::saveBlacklist(
    const std::map<std::string, BlacklistEntry>& blacklist) {
  std::ofstream file(blacklistFile_);
  if (!file.is_open()) {
    return;  // Can't save, but continue
  }

  for (const auto& [ip, entry] : blacklist) {
    file << ip << " " << entry.blockedUntil << "\n";
  }
}

void AuthManager::loadAttempts(std::map<std::string, AttemptInfo>& attempts) {
  std::ifstream file(attemptsFile_);
  if (!file.is_open()) {
    return;  // File doesn't exist yet, that's okay
  }

  std::string line;
  std::time_t now = std::time(nullptr);
  std::time_t cutoff = now - BLACKLIST_DURATION_SECONDS;

  while (std::getline(file, line)) {
    if (line.empty()) continue;

    std::istringstream iss(line);
    std::string key;
    int count;
    std::time_t lastAttempt;

    if (iss >> key >> count >> lastAttempt) {
      // Only load if not too old
      if (lastAttempt >= cutoff) {
        AttemptInfo info;
        info.count = count;
        info.lastAttempt = lastAttempt;
        attempts[key] = info;
      }
    }
  }
}

void AuthManager::saveAttempts(
    const std::map<std::string, AttemptInfo>& attempts) {
  std::ofstream file(attemptsFile_);
  if (!file.is_open()) {
    return;  // Can't save, but continue
  }

  for (const auto& [key, info] : attempts) {
    file << key << " " << info.count << " " << info.lastAttempt << "\n";
  }
}

bool AuthManager::authenticateLDAP(const std::string& username,
                                   const std::string& password) {
  if (username.empty() || password.empty()) {
    return false;
  }

  // Trim whitespace from username/password (handles stray \r\n etc.)
  auto trim = [](const std::string& s) -> std::string {
    const char* ws = " \t\r\n";
    std::size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos) return std::string();
    std::size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
  };

  std::string rawUser = trim(username);
  std::string pass = trim(password);

  if (rawUser.empty() || pass.empty()) {
    return false;
  }

  // Require "LDAP " prefix
  const std::string prefix = "LDAP ";
  if (rawUser.rfind(prefix, 0) != 0) {
    // Username does not start with "LDAP "
    return false;
  }

  // Strip "LDAP " -> actual uid
  std::string user = rawUser.substr(prefix.size());
  if (user.empty()) {
    return false;
  }

  LDAP* ld = nullptr;

  // Build LDAP URI from constants
  std::string uri = std::string("ldap://") + LDAP_HOST + ":" +
                    std::to_string(LDAP_SERVER_PORT);
  int rc = ldap_initialize(&ld, uri.c_str());
  if (rc != LDAP_SUCCESS || ld == nullptr) {
    return false;
  }

  // Set LDAP protocol version to 3
  int ldap_version = LDAP_VERSION3;
  rc = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &ldap_version);
  if (rc != LDAP_SUCCESS) {
    ldap_unbind_ext_s(ld, nullptr, nullptr);
    return false;
  }

  // Build user DN
  std::string dn = "uid=" + user + std::string(",ou=People,") + LDAP_BASE;

  // Prepare credentials
  berval cred;
  cred.bv_val = const_cast<char*>(pass.c_str());
  cred.bv_len = pass.length();

  rc = ldap_sasl_bind_s(ld, dn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr,
                        nullptr, nullptr);

  ldap_unbind_ext_s(ld, nullptr, nullptr);

  return (rc == LDAP_SUCCESS);
}
