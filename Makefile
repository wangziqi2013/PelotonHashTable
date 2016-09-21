
CXX=g++-5
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

benchmark: ./src/HashTable_OA_KVL.cpp ./src/HashTable_CA_CC.cpp ./test/benchmark.cpp
	$(CXX) $(CXXFLAGS) -O3 -DNDEBUG -g $^ -o ./bin/benchmark
    
ca_cc_test: ./src/HashTable_CA_CC.cpp ./test/HashTable_CA_CC_test.cpp
	$(CXX) $(CXXFLAGS) $(OPT_FLAGS) $^ -o ./bin/ca_cc_test

ca_scc_test: ./src/HashTable_CA_SCC.cpp ./test/HashTable_CA_SCC_test.cpp
	$(CXX) $(CXXFLAGS) $(OPT_FLAGS) $^ -o ./bin/ca_scc_test

clean:
	rm -f ./bin/*
	rm -f ./build/*
