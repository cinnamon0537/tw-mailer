#pragma once
#include <memory>
#include <vector>
#include <string>
#include "command_contract.h"
#include "commands.h" // CommandType, command_from, split_lines

class AuthManager;

class CommandFactory {
public:
  static std::unique_ptr<ICommand> create(CommandType type);
};

// Set the global auth manager instance (called from main)
void setAuthManager(AuthManager* manager);
