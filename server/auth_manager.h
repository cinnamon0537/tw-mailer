#pragma once
#include <string>
#include <map>
#include <ctime>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

/**
 * @brief Manages authentication, rate limiting, and IP blacklisting
 */
class AuthManager {
public:
  struct AttemptInfo {
    int count = 0;
    std::time_t lastAttempt = 0;
  };
  
  struct BlacklistEntry {
    std::time_t blockedUntil = 0;
  };

  /**
   * @brief Initialize the auth manager and load persisted blacklist
   * @param blacklistFile Path to file for persisting blacklist
   */
  explicit AuthManager(const std::string& blacklistFile);
  
  ~AuthManager();
  
  /**
   * @brief Check if an IP is blacklisted
   * @param ip Client IP address
   * @return true if IP is blacklisted, false otherwise
   */
  bool isBlacklisted(const std::string& ip);
  
  /**
   * @brief Record a failed login attempt
   * @param ip Client IP address
   * @param username Username that failed
   * @return true if IP should be blacklisted (3 failed attempts), false otherwise
   */
  bool recordFailedAttempt(const std::string& ip, const std::string& username);
  
  /**
   * @brief Record a successful login (reset attempt counter)
   * @param ip Client IP address
   * @param username Username that succeeded
   */
  void recordSuccess(const std::string& ip, const std::string& username);
  
  /**
   * @brief Authenticate user using LDAP
   * @param username LDAP username
   * @param password Password
   * @return true if authentication succeeds, false otherwise
   */
  static bool authenticateLDAP(const std::string& username, const std::string& password);

private:
  std::string blacklistFile_;
  std::string attemptsFile_;
  
  static constexpr int MAX_ATTEMPTS = 3;
  static constexpr int BLACKLIST_DURATION_SECONDS = 60;  // 1 minute
  
  // File-based locking for inter-process synchronization
  int acquireLock(const std::string& lockfile);
  void releaseLock(int fd);
  
  // File-based data operations
  void loadBlacklist(std::map<std::string, BlacklistEntry>& blacklist);
  void saveBlacklist(const std::map<std::string, BlacklistEntry>& blacklist);
  void loadAttempts(std::map<std::string, AttemptInfo>& attempts);
  void saveAttempts(const std::map<std::string, AttemptInfo>& attempts);
  void cleanupExpiredEntries(std::map<std::string, BlacklistEntry>& blacklist,
                             std::map<std::string, AttemptInfo>& attempts);
  std::string makeKey(const std::string& ip, const std::string& username);
};

