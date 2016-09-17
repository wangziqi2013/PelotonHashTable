
#include "../src/HashTable_OA_KVL.h"

using namespace peloton;
using namespace index;

int main() {
  HashTable_OA_KVL<uint64_t, uint64_t> ht{};
  
  ht.Insert(12345, 67890);
  ht.Insert(12345, 67891);
  ht.Insert(12345, 67893);
  ht.Insert(12345, 67892);
  
  auto ret = ht.GetValue(12345);
  
  for(uint32_t i = 0;i < ret.second;i++) {
    printf("%lu\n", *ret.first++);
  }

  return 0;
}
