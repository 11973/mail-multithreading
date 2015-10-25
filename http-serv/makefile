CXX = g++
CXXFLAGS = -Wall -std=c++11 -g
LDFLAGS = -levent
CXXFILES = source.cpp
HFILES = 
OBJECTS = $(CXXFILES:.cpp=.o)
TARGET = wwwd

all : $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CXX) $^ $(LDFLAGS) -o$@

include deps.make
deps.make : $(CXXFILES) $(HFILES)
	gcc -MM $(CXXFILES) > deps.make

clean :
	rm -f $(TARGET) *.o *~ deps.make
