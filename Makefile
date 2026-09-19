CXX ?= c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

harness: tools/harness.cpp sim/terrarium.cpp sim/terrarium.h
	$(CXX) $(CXXFLAGS) -o $@ tools/harness.cpp sim/terrarium.cpp

test: harness
	./harness

clean:
	rm -f harness
.PHONY: test clean
