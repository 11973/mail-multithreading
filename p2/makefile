CXX = g++
CPPFLAGS = -std=c++11 -g
LDFLAGS = -s -g
CFILES_S = chatsrv.cpp
CFILES_C = client.cpp
OBJECTS_S = $(CFILES_S:.cpp=.o)
OBJECTS_C = $(CFILES_C:.cpp=.o)
SERVER = chatsrv 
CLIENT = client

all: $(SERVER) $(CLIENT)

$(SERVER): $(OBJECTS_S)
	$(CXX) $(LDFLAGS) $^ -o $@

$(CLIENT): $(OBJECTS_C)
	$(CXX) $(LDFLAGS) $^ -o $@

include deps.make
deps.make: $(CFILES_S) $(CFILES_C) 
	gcc -MM $^ > deps.make

clean:
	rm -f $(TARGET) *.o *~ *.bak deps.make
