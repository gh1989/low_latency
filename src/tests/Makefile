CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread

all: memory_ordering_example

memory_ordering_example: memory_ordering_example.cpp
	$(CXX) $(CXXFLAGS) -o memory_ordering_example memory_ordering_example.cpp

run: memory_ordering_example
	./memory_ordering_example

clean:
	rm -f memory_ordering_example 