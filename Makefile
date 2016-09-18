
CXX=g++
CXXFLAGS=-Wall -Werror -std=c++11 -g

all:
	@echo "*"
	@echo "* Please select a target to build!"
	@echo "* "

prepare:
	@echo "Creating directories for building..."
	mkdir -p build
	mkdir -p bin

oa_kvl_test: ./src/HashTable_OA_KVL.cpp ./test/HashTable_OA_KVL_test.cpp
	$(CXX) $(CXXFLAGS) $^ -o ./bin/oa_kvl_test

clean:
	rm -f ./bin/*
	rm -f ./build/*
