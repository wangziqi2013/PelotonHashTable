
#include "../src/HashTable_CA_CC.h"

using namespace peloton;
using namespace index;

using HashTable = HashTable_CA_CC<uint64_t, uint64_t, ConstantZero>;

void BasicTest() {
  HashTable ht{30};
  
  for(uint64_t i = 0;i < 128;i++) {

  }
}

int main() {
  HashTable ht{6};
}
