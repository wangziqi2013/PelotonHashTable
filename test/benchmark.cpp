
#include "../src/HashTable_OA_KVL.h"
#include "../src/HashTable_CA_CC.h"
#include <iostream>
#include <random>
#include <chrono>
#include <unordered_map>


using namespace peloton;
using namespace index;

using ValueType = FixedLenValue<64>;

void SequentialInsertTest(uint64_t key_num) {
  std::chrono::time_point<std::chrono::system_clock> start, end;
  start = std::chrono::system_clock::now();
  
  // Insert 1 million keys into std::map
  HashTable_OA_KVL<uint64_t,
                   ValueType,
                   SimpleInt64Hasher,
                   std::equal_to<uint64_t>,
                   LoadFactorPercent<75 >> test_map{1024};
  for(uint64_t i = 0;i < key_num;i++) {
    test_map.Insert(i, ValueType{});
    //test_map.Insert(i, i + 1);
  }

  end = std::chrono::system_clock::now();

  std::chrono::duration<double> elapsed_seconds = end - start;

  std::cout << "HashTable_OA_KVL: " << 1.0 * key_num / (1024 * 1024) / elapsed_seconds.count()
            << " million insertion/sec" << "\n";

  ////////////////////////////////////////////
  // Test read
  std::vector<ValueType> v{};
  v.reserve(100);

  start = std::chrono::system_clock::now();

  int iter = 10;
  for(int j = 0;j < iter;j++) {
    // Read 1 million keys from std::map
    for(uint64_t i = 0;i < key_num;i++) {
      ValueType *t = test_map.GetFirstValue(i);

      v.push_back(*t);
      v.clear();
    }
  }

  end = std::chrono::system_clock::now();

  elapsed_seconds = end - start;
  std::cout << "HashTable_OA_KVL: " << (1.0 * iter * key_num) / (1024 * 1024) / elapsed_seconds.count()
            << " million read/sec" << "\n";

  std::cout << "Table size = "
            << test_map.GetEntryCount() \
            << "; " \
            << "Resize threshold = " \
            << test_map.GetResizeThreshold()
            << std::endl;
            
  std::cout << "Load factor = " \
            << test_map.GetLoadFactor() \
            << std::endl;

  std::cout << "Maximum search sequence length: " \
            << test_map.GetMaxSearchSequenceLength() \
            << std::endl;
            
  std::cout << "Mean search sequence length: " \
            << test_map.GetMeanSearchSequenceLength()
            << std::endl;
            
  std::cout << "Maximum probe length: " \
            << test_map.GetMaxSearchProbeLength()
            << std::endl;
            
  double mean = test_map.GetMeanSearchProbeLength();
            
  std::cout << "Mean probe length: " \
            << mean
            << std::endl;
            
  std::cout << "Probe length standard deviation: " \
            << test_map.GetStdDevSearchProbeLength(mean)
            << std::endl;

  return;
}

void UnorderedMapSequentialInsertTest(uint64_t key_num) {
  std::chrono::time_point<std::chrono::system_clock> start, end;
  start = std::chrono::system_clock::now();

  // Insert 1 million keys into std::map
  std::unordered_multimap<uint64_t, ValueType, SimpleInt64Hasher> test_map{};
  for(uint64_t i = 0;i < key_num;i++) {
    test_map.insert({i, ValueType{}});
    //test_map.insert({i, i + 1});
  }

  end = std::chrono::system_clock::now();

  std::chrono::duration<double> elapsed_seconds = end - start;

  std::cout << "std::unordered_multimap: " << 1.0 * key_num / (1024 * 1024) / elapsed_seconds.count()
            << " million insertion/sec" << "\n";

  ////////////////////////////////////////////
  // Test read
  std::vector<ValueType> v{};
  v.reserve(100);

  start = std::chrono::system_clock::now();

  int iter = 10;
  for(int j = 0;j < iter;j++) {
    // Read 1 million keys from std::map
    for(uint64_t i = 0;i < key_num;i++) {
      const ValueType &t = test_map.find(i)->second;

      v.push_back(t);
      v.clear();
    }
  }

  end = std::chrono::system_clock::now();

  elapsed_seconds = end - start;
  std::cout << "std::unordered_multimap: " << (1.0 * iter * key_num) / (1024 * 1024) / elapsed_seconds.count()
            << " million read/sec" << "\n";

  return;
}

void CA_CC_SequentialInsertTest(uint64_t key_num) {
  std::chrono::time_point<std::chrono::system_clock> start, end;
  start = std::chrono::system_clock::now();

  // Insert 1 million keys into std::map
  HashTable_CA_CC<uint64_t,
                  ValueType,
                  SimpleInt64Hasher,
                  std::equal_to<uint64_t>,
                  LoadFactorPercent<400>> test_map{1024};
  for(uint64_t i = 0;i < key_num;i++) {
    test_map.Insert(i, ValueType{});
    //test_map.Insert(i, i + 1);
  }

  end = std::chrono::system_clock::now();

  std::chrono::duration<double> elapsed_seconds = end - start;

  std::cout << "HashTable_CA_CC: " << 1.0 * key_num / (1024 * 1024) / elapsed_seconds.count()
            << " million insertion/sec" << "\n";

  ////////////////////////////////////////////
  // Test read
  std::vector<ValueType> v{};
  v.reserve(100);

  start = std::chrono::system_clock::now();

  int iter = 10;
  for(int j = 0;j < iter;j++) {
    // Read 1 million keys from std::map
    for(uint64_t i = 0;i < key_num;i++) {
      test_map.GetValue(i, &v);

      v.clear();
    }
  }

  end = std::chrono::system_clock::now();

  elapsed_seconds = end - start;
  std::cout << "HashTable_CA_CC: " << (1.0 * iter * key_num) / (1024 * 1024) / elapsed_seconds.count()
            << " million read/sec" << "\n";

  return;
}

/*
 * main() - Main test routine
 *
 * |------------------------|-----------------------------|
 * |       Command          |         Explanation         |
 * |------------------------|-----------------------------|
 * | ./benchmark            | Prints help message         |
 * | ./benchmark --seq      | Runs sequential test        |
 * | ./benchmark --random   | Runs random workload test   |
 * |------------------------|-----------------------------|
 */
int main(int argc, char **argv) {
  // Make sure we have correct number of arguments
  if(argc == 1) {
    printf("Please use command line argument to run test suites!\n");
    
    return 0;
  } else if(argc > 2) {
    printf("Too many arguments\n");
    
    return 0;
  }
  
  char *p = argv[1];
  
  if(strcmp(p, "--seq") == 0) {
    SequentialInsertTest(6 * 1024 * 1024);
    UnorderedMapSequentialInsertTest(6 * 1024 * 1024);
    CA_CC_SequentialInsertTest(6 * 1024 * 1024);
  } else if(strcmp(p, "--random") == 0) {

  } else {
    printf("Unknown argument: %s\n", p);
  }
  
  return 0;
}
