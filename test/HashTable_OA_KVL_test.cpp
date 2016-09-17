
#include "../src/HashTable_OA_KVL.h"

using namespace peloton;
using namespace index;

using HashTable = HashTable_OA_KVL<uint64_t, uint64_t, ConstantZero>;

void PrintValuesForKey(HashTable *ht_p, uint64_t key) {
  auto ret = ht_p->GetValue(key);

  for(uint32_t i = 0;i < ret.second;i++) {
    printf("%lu ", *ret.first++);
  }
  
  putchar('\n');
  
  return;
}

int main() {
  HashTable ht{};
  
  ht.Insert(12345, 67890);
  ht.Insert(12345, 67891);
  ht.Insert(12345, 67893);
  ht.Insert(12345, 67892);
  
  ht.Insert(12346, 111);
  ht.Insert(12346, 112);
  ht.Insert(12347, 222);
  
  ht.Insert(12346, 113);
  ht.Insert(12347, 223);
  ht.Insert(12346, 114);
  ht.Insert(12347, 224);
  
  PrintValuesForKey(&ht, 12347);
  PrintValuesForKey(&ht, 12345);
  PrintValuesForKey(&ht, 12346);

  return 0;
}
