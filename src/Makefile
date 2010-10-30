OBJS= twff.o Server.o bailsock.o
LINKF= -lpthread
LAUNCH= twff
CXX= g++
CXXFLAGS= -std=gnu++98 -pedantic -g -Wall -Wextra -Wconversion -Wno-variadic-macros -O0

.PHONY: all
all: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(LAUNCH) $(OBJS) $(LINKF)
	
.PHONY: clean
clean:
	rm -f $(LAUNCH) $(OBJS)
	
%.o: %.cpp %.h
