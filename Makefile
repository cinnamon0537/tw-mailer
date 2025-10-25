CXX      = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra
CPPFLAGS = -I. -Icommon         # <-- look for headers in project root AND common/
BIN      = bin

COMMON_SRC  = common/net.cpp    # <-- moved file
CLIENT_SRC  = client/main.cpp $(COMMON_SRC)
SERVER_SRC  = server/main.cpp $(COMMON_SRC)
# If you added a factory later, extend SERVER_SRC with server/command_factory.cpp

all: $(BIN)/twmailer-client $(BIN)/twmailer-server

$(BIN)/twmailer-client: $(CLIENT_SRC)
	mkdir -p $(BIN)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $(CLIENT_SRC)

$(BIN)/twmailer-server: $(SERVER_SRC)
	mkdir -p $(BIN)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $(SERVER_SRC)

clean:
	rm -rf $(BIN)

.PHONY: all clean
