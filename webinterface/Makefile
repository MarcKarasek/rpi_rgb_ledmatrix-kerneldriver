# Creating Practical Sockets Library
#
#   -lpracticalsocket
##
OBJECTS=PracticalSocket.o
TARGET=libpracticalsocket.a

CXXFLAGS=-Wall -O3 -ansi -pedantic -g

$(TARGET) : $(OBJECTS)
	ar rcs $@ $^

PracticalSocket.o : PracticalSocket.cpp PracticalSocket.h

%.o : %.cpp
	$(CXX) -I$(INCDIR) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)
