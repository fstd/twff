OBJS= twff.o Server.o bailsock.o
LINKF= -lpthread
LAUNCH= twff
CXX= g++
CXXFLAGS= -std=gnu++98 -g -O0 -Wall -Wextra -Wconversion -D_WITH_GETLINE

.PHONY: all
all: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(LAUNCH) $(OBJS) $(LINKF)
	
.PHONY: clean
clean:
	rm -f $(LAUNCH) $(OBJS)
	
%.o: %.cpp %.h
