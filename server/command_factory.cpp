#include "command_factory.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace fs = std::filesystem;

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

static int next_message_id(const fs::path& dir) {
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

class SendCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
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
    // EXPECT: lines[0]=LIST, [1]=user
    if (lines.size() < 2) return {false, "ERR\n"};
    const std::string& user = lines[1];
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
      auto dot  = name.find('.');
      auto stem = (dot == std::string::npos) ? name : name.substr(0, dot);
      if (is_number(stem)) {
        msgs.emplace_back(to_int(stem), entry.path());
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
    // EXPECT: lines[0]=READ, [1]=user, [2]=id
    if (lines.size() < 3) return {false, "ERR\n"};
    const std::string& user = lines[1];
    int id = to_int(lines[2]);
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
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    // EXPECT: lines[0]=DEL, [1]=user, [2]=id
    if (lines.size() < 3) return {false, "ERR\n"};
    const std::string& user = lines[1];
    int id = to_int(lines[2]);
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
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    (void)ctx; (void)lines;
    // Per spec: no response; just close
    return {.shouldClose = true, .response = ""};
  }
};

// ----------------- Factory -----------------

std::unique_ptr<ICommand> CommandFactory::create(CommandType type) {
  switch (type) {
    case CommandType::SEND: return std::make_unique<SendCommand>();
    case CommandType::LIST: return std::make_unique<ListCommand>();
    case CommandType::READ: return std::make_unique<ReadCommand>();
    case CommandType::DEL:  return std::make_unique<DelCommand>();
    case CommandType::QUIT: return std::make_unique<QuitCommand>();
    default:                return nullptr;
  }
}
