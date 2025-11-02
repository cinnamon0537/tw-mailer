CXX      = g++
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -O0 -g -fno-omit-frame-pointer
CPPFLAGS = -I. -Icommon -Iserver     # header search paths
BIN      = bin

# shared / common
COMMON_SRC  = common/net.cpp

# targets
CLIENT_SRC  = client/client.cpp $(COMMON_SRC)
SERVER_SRC  = server/server.cpp server/command_factory.cpp $(COMMON_SRC)

all: $(BIN)/twmailer-client $(BIN)/twmailer-server

$(BIN)/twmailer-client: $(CLIENT_SRC)
	mkdir -p $(BIN)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^

$(BIN)/twmailer-server: $(SERVER_SRC)
	mkdir -p $(BIN)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^

clean:
	rm -rf $(BIN)

.PHONY: all clean
