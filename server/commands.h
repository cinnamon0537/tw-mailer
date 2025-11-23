#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class CommandType { LOGIN, SEND, LIST, READ, DEL, QUIT, UNKNOWN };

inline CommandType command_from(std::string_view s) {
  static const std::unordered_map<std::string_view, CommandType> map{
      {"LOGIN", CommandType::LOGIN}, {"SEND", CommandType::SEND},
      {"LIST", CommandType::LIST},   {"READ", CommandType::READ},
      {"DEL", CommandType::DEL},     {"QUIT", CommandType::QUIT},
  };
  if (auto it = map.find(s); it != map.end()) return it->second;
  return CommandType::UNKNOWN;
}

inline std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t pos = s.find('\n', start);
    if (pos == std::string::npos) {
      out.emplace_back(s.substr(start));
      break;
    }
    out.emplace_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  // handle trailing newline -> last element may be empty; that’s fine
  return out;
}
