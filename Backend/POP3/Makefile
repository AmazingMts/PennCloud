CXX = g++
CXXFLAGS = -O2 -std=c++17 -I.. $(foreach dir, $(SRC_DIRS), -I$(dir))
LDFLAGS = -lpthread -lresolv
SRC_DIRS = ../KVStorage ../SMTP ../Coordinator ../../Shared ../KVStorageSrc
SRC = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*Dispatcher*.cc)) $(wildcard *.cc ../*.cc ../../Shared/*.cc ../KVStorageSrc/*.cc) ../SMTP/relay.cc
OBJ = $(SRC:.cc=.o)
TARGET = pop3

all: $(TARGET) # Executable

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) $(LDFLAGS) -o $(TARGET)

# Build all files using wildcards
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $^

clean:
	rm -rf $(OBJ) $(TARGET)