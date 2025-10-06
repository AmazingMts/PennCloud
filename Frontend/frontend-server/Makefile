# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++20 -Wall -O2 -pthread

# Targets
TARGETS = frontend-server

# Source files for frontend-server
SRCS_frontend_server = \
	frontend-server.cpp \
	email-handlers.cpp \
    drive-handlers.cpp \
	user-handlers.cpp \
	lbregister.cpp \
	admin-handlers.cpp \
	mail-service.cpp \
	cookie-handler.cpp \
	utils.cpp \
	../../Shared/IStorageService.cc \
	../../Shared/CoordinatorService.cpp \
	../../Shared/RequestAssembler.cpp \
	../../Shared/ResponseParser.cpp \
	../../Shared/KVServerCommand.cpp \
	../../Shared/SimpleSocketReader.cc

# Object files
OBJS_frontend_server = $(SRCS_frontend_server:.cpp=.o)
OBJS_frontend_server := $(OBJS_frontend_server:.cc=.o)

# Default target
all: admin-server lb-server $(TARGETS)

# Build frontend-server
frontend-server: $(OBJS_frontend_server)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Build admin-server from ../admin
admin-server:
	$(MAKE) -C ../admin

# Build load-balancer from ../load-balancer
lb-server:
	$(MAKE) -C ../load-balancer

# Compile rules
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

../../%.o: ../../%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

../../%.o: ../../%.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean target
clean:
	rm -fv $(TARGETS) *.o *~ ../../Shared/*.o
	$(MAKE) -C ../admin clean
	$(MAKE) -C ../load-balancer clean

.PHONY: all clean admin-server lb-server

# Create a zip archive of the project
pack:
	zip -r penncloud.zip . -x "*.o" "*.zip" "*~" "__pycache__/*" "*/__pycache__/*" "*.DS_Store" "*.out" "*.log"
