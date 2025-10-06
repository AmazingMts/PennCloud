# 编译器设置
CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread -O2

# 文件路径
SRC = load-balancer.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = load-balancer

# 默认目标：编译
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<

# 运行程序
run: all
	./$(TARGET)

# 清除临时文件
clean:
	rm -f $(TARGET) *.o
