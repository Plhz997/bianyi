CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
LDFLAGS ?= -lm

TARGET := compiler
SRCS := $(wildcard src/*.cpp) $(wildcard src/frontend/*.cpp)
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET) tests/basic.sy --dump-ast >/tmp/bianyi_ast.txt
	./$(TARGET) tests/basic.sy -S -o /tmp/bianyi_basic.s

clean:
	rm -f $(TARGET) $(OBJS)

