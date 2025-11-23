#include "command_factory.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "auth_manager.h"

namespace fs = std::filesystem;

// Global auth manager instance (initialized in main)
static AuthManager* g_authManager = nullptr;

/**
 * @brief Set the global auth manager instance
 */
void setAuthManager(AuthManager* manager) { g_authManager = manager; }

// ----------------- small helpers -----------------

static fs::path user_dir(const Context& ctx, const std::string& user) {
  return fs::path(ctx.spoolDir) / user;
}

static bool is_number(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

static int to_int(const std::string& s) {
  try {
    return std::stoi(s);
  } catch (...) {
    return -1;
  }
}

static int next_message_id(const fs::path& dir) {
  int max_id = 0;
  if (!fs::exists(dir)) return 1;
  for (auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    auto name = entry.path().filename().string();  // e.g. "12.txt"
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
  CommandOutcome execute(Context& ctx,
                         const std::vector<std::string>& lines) override {
    // EXPECT: lines[0]=LOGIN, [1]=username, [2]=password
    if (lines.size() < 3) {
      return {false, "ERR\n"};
    }

    if (!g_authManager) {
      return {false, "ERR\n"};
    }

    const std::string& rawUsername = lines[1];  // e.g. "LDAP if25b251"
    const std::string& password    = lines[2];

    // Check if IP is blacklisted
    if (g_authManager->isBlacklisted(ctx.clientIP)) {
      return {false, "ERR\n"};
    }

    // Try LDAP authentication with the raw username (must still start with "LDAP ")
    bool authSuccess = AuthManager::authenticateLDAP(rawUsername, password);

    // Normalize username for attempt tracking and session (strip "LDAP " prefix)
    std::string normalizedUser = rawUsername;
    const std::string prefix = "LDAP ";
    if (normalizedUser.rfind(prefix, 0) == 0) {
      normalizedUser = normalizedUser.substr(prefix.size());  // "if25b251"
    }

    if (authSuccess) {
      // Record success using normalized username (no spaces)
      g_authManager->recordSuccess(ctx.clientIP, normalizedUser);

      // Use clean username for session
      ctx.authenticatedUser = normalizedUser;

      return {false, "OK\n"};
    } else {
      // Record failed attempt (may blacklist IP if 3 attempts reached)
      bool blacklisted =
          g_authManager->recordFailedAttempt(ctx.clientIP, normalizedUser);
      if (blacklisted) {
        // IP is now blacklisted for 1 minute
        return {false, "ERR\n"};
      }
      return {false, "ERR\n"};
    }
  }
};


class SendCommand final : public ICommand {
 public:
  CommandOutcome execute(Context& ctx,
                         const std::vector<std::string>& lines) override {
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }

    // EXPECT: lines[0]=SEND, [1]=to, [2]=subject, [3..]=body lines
    // Sender is automatically set from authenticated user session
    if (lines.size() < 3) {
      return {false, "ERR\n"};
    }
    const std::string& from =
        ctx.authenticatedUser;  // Use authenticated user as sender
    const std::string& to = lines[1];
    const std::string& subj = lines[2];

    // join remaining lines as body (with '\n' between original lines)
    std::string body;
    if (lines.size() > 3) {
      // reconstruct body with newlines
      for (size_t i = 3; i < lines.size(); ++i) {
        if (i > 3) body.push_back('\n');
        body += lines[i];
      }
      body.push_back('\n');  // end with newline for readability
    }

    fs::path udir = user_dir(ctx, to);
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
        << to << "\n"
        << subj << "\n"
        << "\n";
    if (!body.empty()) out << body;

    return {false, "OK\n"};
  }
};

class ListCommand final : public ICommand {
 public:
  CommandOutcome execute(Context& ctx,
                         const std::vector<std::string>& lines) override {
    (void)lines;  // No parameters needed - uses authenticated user from session
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }

    // EXPECT: lines[0]=LIST
    // Username is automatically set from authenticated user session
    const std::string& user = ctx.authenticatedUser;
    fs::path udir = user_dir(ctx, user);

    if (!fs::exists(udir) || !fs::is_directory(udir)) {
      // no messages
      return {false, "0\n"};
    }

    // collect numeric files and sort by id ascending
    std::vector<std::pair<int, fs::path>> msgs;
    for (auto& entry : fs::directory_iterator(udir)) {
      if (!entry.is_regular_file()) continue;
      auto name = entry.path().filename().string();
      auto dot = name.find('.');
      auto stem = (dot == std::string::npos) ? name : name.substr(0, dot);
      if (is_number(stem)) {
        msgs.emplace_back(to_int(stem), entry.path());
      }
    }
    std::sort(msgs.begin(), msgs.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

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
  CommandOutcome execute(Context& ctx,
                         const std::vector<std::string>& lines) override {
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }

    // EXPECT: lines[0]=READ, [1]=id
    // Username is automatically set from authenticated user session
    if (lines.size() < 2) return {false, "ERR\n"};
    const std::string& user = ctx.authenticatedUser;
    int id = to_int(lines[1]);
    if (id <= 0) return {false, "ERR\n"};

    fs::path file = user_dir(ctx, user) / (std::to_string(id) + ".txt");
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
  CommandOutcome execute(Context& ctx,
                         const std::vector<std::string>& lines) override {
    // Check authentication
    if (ctx.authenticatedUser.empty()) {
      return {false, "ERR\n"};
    }

    // EXPECT: lines[0]=DEL, [1]=id
    // Username is automatically set from authenticated user session
    if (lines.size() < 2) return {false, "ERR\n"};
    const std::string& user = ctx.authenticatedUser;
    int id = to_int(lines[1]);
    if (id <= 0) return {false, "ERR\n"};

    fs::path file = user_dir(ctx, user) / (std::to_string(id) + ".txt");
    std::error_code ec;
    bool ok = fs::remove(file, ec);
    if (!ok || ec) return {false, "ERR\n"};
    return {false, "OK\n"};
  }
};

class QuitCommand final : public ICommand {
 public:
  CommandOutcome execute(Context& ctx,
                         const std::vector<std::string>& lines) override {
    (void)ctx;
    (void)lines;
    // Per spec: no response; just close
    return {.shouldClose = true, .response = ""};
  }
};

// ----------------- Factory -----------------

std::unique_ptr<ICommand> CommandFactory::create(CommandType type) {
  switch (type) {
    case CommandType::LOGIN:
      return std::make_unique<LoginCommand>();
    case CommandType::SEND:
      return std::make_unique<SendCommand>();
    case CommandType::LIST:
      return std::make_unique<ListCommand>();
    case CommandType::READ:
      return std::make_unique<ReadCommand>();
    case CommandType::DEL:
      return std::make_unique<DelCommand>();
    case CommandType::QUIT:
      return std::make_unique<QuitCommand>();
    default:
      return nullptr;
  }
}
