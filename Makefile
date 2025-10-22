CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

BIN = bin
CLIENT_SRC = client/main.cpp
SERVER_SRC = server/main.cpp

all: $(BIN)/twmailer-client $(BIN)/twmailer-server

$(BIN)/twmailer-client: $(CLIENT_SRC)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS)

$(BIN)/twmailer-server: $(SERVER_SRC)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

clean:
	rm -rf $(BIN)
