CXX      = c++
CXXFLAGS = -std=c++17 -O2 -Wall -pthread

all: server client

server: src/server.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/server.cpp

client: src/client.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/client.cpp

test: server
	./server --selftest 10

clean:
	rm -f server client

.PHONY: all test clean
