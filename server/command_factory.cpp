#include "command_factory.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

namespace fs = std::filesystem;

// ----------------- authentication (mock - will be replaced with LDAP) -----------------

/**
 * @brief Mock authentication function
 * @param username Username to authenticate
 * @param password Password to verify
 * @return true if authentication succeeds, false otherwise
 * 
 * TODO: Replace with LDAP authentication in next commit
 */
static bool authenticate_user(const std::string& username, const std::string& password) {
  // Mock authentication: accept any non-empty username/password for now
  // In production, this will query LDAP
  return !username.empty() && !password.empty();
}

// ----------------- file locking helpers -----------------

/**
 * @brief RAII wrapper for file-based advisory locks using flock()
 * Provides process-level synchronization for multi-process applications
 */
class FileLock {
public:
  enum class LockType {
    SHARED,    // Shared (read) lock - multiple processes can hold simultaneously
    EXCLUSIVE  // Exclusive (write) lock - only one process can hold
  };

  FileLock(const fs::path& lockfile, LockType type = LockType::EXCLUSIVE, bool blocking = true)
    : fd_(-1), locked_(false) {
    // Create lock file if it doesn't exist
    fd_ = open(lockfile.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd_ < 0) return;
    
    int operation = (type == LockType::SHARED) ? LOCK_SH : LOCK_EX;
    if (!blocking) operation |= LOCK_NB;
    
    if (flock(fd_, operation) == 0) {
      locked_ = true;
    }
  }
  
  ~FileLock() {
    unlock();
  }
  
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;
  
  FileLock(FileLock&& other) noexcept : fd_(other.fd_), locked_(other.locked_) {
    other.fd_ = -1;
    other.locked_ = false;
  }
  
  FileLock& operator=(FileLock&& other) noexcept {
    if (this != &other) {
      unlock();
      fd_ = other.fd_;
      locked_ = other.locked_;
      other.fd_ = -1;
      other.locked_ = false;
    }
    return *this;
  }
  
  bool is_locked() const { return fd_ >= 0 && locked_; }
  
  void unlock() {
    if (locked_ && fd_ >= 0) {
      flock(fd_, LOCK_UN);
      locked_ = false;
    }
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

private:
  int fd_;
  bool locked_;
};

// ----------------- small helpers -----------------

static fs::path user_dir(const Context& ctx, const std::string& user) {
  return fs::path(ctx.spoolDir) / user;
}

static bool is_number(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

static int to_int(const std::string& s) {
  try { return std::stoi(s); } catch (...) { return -1; }
}

/**
 * @brief Get the next available message ID for a user directory
 * @param dir User directory path
 * @return Next available message ID
 * 
 * This function is protected by file locking to prevent race conditions
 * when multiple processes try to create messages simultaneously.
 */
static int next_message_id(const fs::path& dir) {
  // Lock file for this user's directory to prevent race conditions
  fs::path lockfile = dir / ".lock";
  FileLock lock(lockfile, FileLock::LockType::EXCLUSIVE);
  
  if (!lock.is_locked()) {
    // If we can't acquire lock, return a safe fallback
    return 1;
  }
  
  int max_id = 0;
  if (!fs::exists(dir)) return 1;
  for (auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    auto name = entry.path().filename().string(); // e.g. "12.txt"
    auto dot = name.find('.');
    auto stem = (dot == std::string::npos) ? name : name.substr(0, dot);
    if (is_number(stem)) {
      max_id = std::max(max_id, to_int(stem));
    }
  }
  return max_id + 1;
}

// read the 3rd logical line (= subject) of a message file
static std::string read_subject(const fs::path& file) {
  std::ifstream in(file);
  if (!in) return "";
  std::string from, to, subject;
  std::getline(in, from);
  std::getline(in, to);
  std::getline(in, subject);
  return subject;
}

// read the whole file (used by READ)
static std::string slurp(const fs::path& file) {
  std::ifstream in(file);
  if (!in) return "";
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

// ----------------- Commands -----------------

class LoginCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    // EXPECT: lines[0]=LOGIN, [1]=username, [2]=password
    if (lines.size() < 3) {
      return {false, "ERR\n"};
    }
    
    const std::string& username = lines[1];
    const std::string& password = lines[2];
    
    if (authenticate_user(username, password)) {
      ctx.authenticatedUser = username;
      return {false, "OK\n"};
    } else {
      return {false, "ERR\n"};
    }
  }
};

class SendCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }
    
    // EXPECT: lines[0]=SEND, [1]=from, [2]=to, [3]=subject, [4..]=body lines (already joined by client)
    if (lines.size() < 4) {
      return {false, "ERR\n"};
    }
    const std::string& from = lines[1];
    const std::string& to   = lines[2];
    const std::string& subj = lines[3];

    // join remaining lines as body (with '\n' between original lines)
    std::string body;
    if (lines.size() > 4) {
      // reconstruct body with newlines
      for (size_t i = 4; i < lines.size(); ++i) {
        if (i > 4) body.push_back('\n');
        body += lines[i];
      }
      body.push_back('\n'); // end with newline for readability
    }

    fs::path udir = user_dir(ctx, to);
    
    // Lock the user directory for exclusive access during message creation
    fs::path lockfile = udir / ".lock";
    FileLock lock(lockfile, FileLock::LockType::EXCLUSIVE);
    
    if (!lock.is_locked()) {
      return {false, "ERR\n"};
    }
    
    std::error_code ec;
    fs::create_directories(udir, ec);
    if (ec) {
      return {false, "ERR\n"};
    }

    int id = next_message_id(udir);
    fs::path file = udir / (std::to_string(id) + ".txt");

    std::ofstream out(file);
    if (!out) return {false, "ERR\n"};

    // simple format: 3 header lines + blank + body
    out << from << "\n"
        << to   << "\n"
        << subj << "\n"
        << "\n";
    if (!body.empty()) out << body;

    return {false, "OK\n"};
  }
};

class ListCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }
    
    // EXPECT: lines[0]=LIST, [1]=user
    if (lines.size() < 2) return {false, "ERR\n"};
    const std::string& user = lines[1];
    
    // Verify user can only list their own mailbox
    if (user != ctx.authenticatedUser) {
      return {false, "ERR\n"};
    }
    
    fs::path udir = user_dir(ctx, user);
    
    // Lock the user directory for shared access during listing
    fs::path lockfile = udir / ".lock";
    FileLock lock(lockfile, FileLock::LockType::SHARED);
    
    if (!lock.is_locked() && fs::exists(udir)) {
      // If directory exists but we can't lock, return error
      return {false, "ERR\n"};
    }

    if (!fs::exists(udir) || !fs::is_directory(udir)) {
      // no messages
      return {false, "0\n"};
    }

    // collect numeric files and sort by id ascending
    std::vector<std::pair<int, fs::path>> msgs;
    if (fs::exists(udir) && fs::is_directory(udir)) {
      for (auto& entry : fs::directory_iterator(udir)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        // Skip lock files
        if (name == ".lock") continue;
        auto dot  = name.find('.');
        auto stem = (dot == std::string::npos) ? name : name.substr(0, dot);
        if (is_number(stem)) {
          msgs.emplace_back(to_int(stem), entry.path());
        }
      }
    }
    std::sort(msgs.begin(), msgs.end(),
              [](auto& a, auto& b){ return a.first < b.first; });

    std::ostringstream reply;
    reply << msgs.size() << "\n";
    for (auto& [id, path] : msgs) {
      std::string subject = read_subject(path);
      reply << subject << "\n";
    }
    return {false, reply.str()};
  }
};

class ReadCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }
    
    // EXPECT: lines[0]=READ, [1]=user, [2]=id
    if (lines.size() < 3) return {false, "ERR\n"};
    const std::string& user = lines[1];
    
    // Verify user can only read their own mailbox
    if (user != ctx.authenticatedUser) {
      return {false, "ERR\n"};
    }
    
    int id = to_int(lines[2]);
    if (id <= 0) return {false, "ERR\n"};

    fs::path udir = user_dir(ctx, user);
    fs::path file = udir / (std::to_string(id) + ".txt");
    
    // Lock the user directory for shared access during read
    fs::path lockfile = udir / ".lock";
    FileLock lock(lockfile, FileLock::LockType::SHARED);
    
    if (!fs::exists(file)) return {false, "ERR\n"};

    std::string content = slurp(file);
    if (content.empty()) return {false, "ERR\n"};

    // Spec hint: prefix OK\n then full stored message
    std::string reply = "OK\n" + content;
    return {false, reply};
  }
};

class DelCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }
    
    // EXPECT: lines[0]=DEL, [1]=user, [2]=id
    if (lines.size() < 3) return {false, "ERR\n"};
    const std::string& user = lines[1];
    
    // Verify user can only delete from their own mailbox
    if (user != ctx.authenticatedUser) {
      return {false, "ERR\n"};
    }
    
    int id = to_int(lines[2]);
    if (id <= 0) return {false, "ERR\n"};

    fs::path udir = user_dir(ctx, user);
    fs::path file = udir / (std::to_string(id) + ".txt");
    
    // Lock the user directory for exclusive access during deletion
    fs::path lockfile = udir / ".lock";
    FileLock lock(lockfile, FileLock::LockType::EXCLUSIVE);
    
    if (!lock.is_locked()) {
      return {false, "ERR\n"};
    }
    
    if (!fs::exists(file)) return {false, "ERR\n"};
    
    std::error_code ec;
    bool ok = fs::remove(file, ec);
    if (!ok || ec) return {false, "ERR\n"};
    return {false, "OK\n"};
  }
};

class QuitCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    (void)ctx; (void)lines;
    // Per spec: no response; just close
    return {.shouldClose = true, .response = ""};
  }
};

// ----------------- Factory -----------------

std::unique_ptr<ICommand> CommandFactory::create(CommandType type) {
  switch (type) {
    case CommandType::LOGIN: return std::make_unique<LoginCommand>();
    case CommandType::SEND: return std::make_unique<SendCommand>();
    case CommandType::LIST: return std::make_unique<ListCommand>();
    case CommandType::READ: return std::make_unique<ReadCommand>();
    case CommandType::DEL:  return std::make_unique<DelCommand>();
    case CommandType::QUIT: return std::make_unique<QuitCommand>();
    default:                return nullptr;
  }
}
