CXX = g++
CXXFLAGS = -Wall -std=c++17 -g
LDFLAGS = -pthread -lstdc++fs # -lstdc++fs for older g++ versions if filesystem isn't linked by default

TARGET = client
SRCS = client.cpp

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o