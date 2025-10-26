#pragma once
#include <string>
#include <vector>

struct Context {
  int clientFd;             // Socket des Clients
  std::string spoolDir;     // Mail-Spool-Verzeichnis
};

// Ergebnis eines Kommandos (ohne echte Implementierung)
struct CommandOutcome {
  bool shouldClose = false;   // true -> Verbindung schließen (z.B. QUIT)
  std::string response;       // length-prefixed Antwort (OK/ERR/…); leer = keine Antwort
};

class ICommand {
public:
  virtual ~ICommand() = default;
  // lines[0] ist der Befehl (SEND/LIST/...), danach Parameter
  virtual CommandOutcome execute(Context& ctx, const std::vector<std::string>& lines) = 0;
};
