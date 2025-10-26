#include "command_factory.h"
#include <iostream>

// --- Stub-Implementierungen (nur Vertrag, noch keine echte Logik) ---

class SendCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    (void)ctx; (void)lines;
    // Platzhalter: später Mails speichern; jetzt minimal antworten
    return {.shouldClose = false, .response = "OK\n"};
  }
};

class ListCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    (void)ctx; (void)lines;
    // Platzhalter: später echte Liste; jetzt 0
    return {.shouldClose = false, .response = "0\n"};
  }
};

class ReadCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    (void)ctx; (void)lines;
    // Platzhalter: noch nicht implementiert
    return {.shouldClose = false, .response = "ERR\n"};
  }
};

class DelCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    (void)ctx; (void)lines;
    // Platzhalter: noch nicht implementiert
    return {.shouldClose = false, .response = "OK\n"};
  }
};

class QuitCommand final : public ICommand {
public:
  CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) override {
    (void)ctx; (void)lines;
    // Spezifikation: keine Antwort, Verbindung schließen
    return {.shouldClose = true, .response = ""};
  }
};

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
