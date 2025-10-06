all: admin-server

OBJS = admin-server.o admin-handlers.o \
       ../../Shared/CoordinatorService.o \
       ../../Shared/IStorageService.o \
       ../../Shared/RequestAssembler.o \
       ../../Shared/ResponseParser.o \
       ../../Shared/KVServerCommand.o \
       ../../Shared/SimpleSocketReader.o

CXX = g++
CXXFLAGS = -std=c++20 -Wall -O2 -pthread

admin-server: $(OBJS)
	$(CXX) $(CXXFLAGS) -o admin-server $(OBJS)

clean:
	rm -fv admin-server *.o