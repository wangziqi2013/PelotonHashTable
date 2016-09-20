
CXX=g++
CXXFLAGS=-Wall -Werror -std=c++11
OPT_FLAGS=-g

all:
	@echo "*"
	@echo "* Please select a target to build!"
	@echo "* "

prepare:
	@echo "Creating directories for building..."
	mkdir -p build
	mkdir -p bin

oa_kvl_test: ./src/HashTable_OA_KVL.cpp ./test/HashTable_OA_KVL_test.cpp
	$(CXX) $(CXXFLAGS) $(OPT_FLAGS) $^ -o ./bin/oa_kvl_test

oa_kvl_benchmark: ./src/HashTable_OA_KVL.cpp ./test/HashTable_OA_KVL_benchmark.cpp
	$(CXX) $(CXXFLAGS) -Ofast  $^ -o ./bin/oa_kvl_benchmark
    
ca_cc_test: ./src/HashTable_CA_CC.cpp ./test/HashTable_CA_CC_test.cpp
	$(CXX) $(CXXFLAGS) $(OPT_FLAGS) $^ -o ./bin/ca_cc_test

clean:
	rm -f ./bin/*
	rm -f ./build/*
