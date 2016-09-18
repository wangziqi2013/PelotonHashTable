
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

void IteratorTest() {
  dbg_printf("========== Iterator Test ==========\n");
  
  HashTable ht{};
  
  // Test data: 1 -> 1000, ..., 1002
  //            2 -> 2000, ..., 2004
  //            3 -> 3000
  //            4 -> 4000
  
  ht.Insert(1, 1001);
  ht.Insert(4, 4000);
  ht.Insert(1, 1002);
  
  ht.Insert(2, 2000);
  ht.Insert(2, 2002);
  ht.Insert(1, 1000);
  ht.Insert(2, 2001);
  
  ht.Insert(2, 2004);
  
  ht.Insert(3, 3000);
  ht.Insert(2, 2003);
  
  
  auto it2 = ht.End();
  for(auto it = ht.Begin();it != it2;++it) {
    printf("%lu -> %lu\n", it.GetKey(), *it);
  }
  
  return;
}

void ResizeTest() {
  dbg_printf("========== Resize Test ==========\n");
  
  HashTable ht{2};
  
  for(uint64_t i = 0;i < 239;i++) {
    ht.Insert(i, i);
  }
  
  auto it1 = ht.Begin();
  auto it2 = ht.End();
  
  while(it1 != it2) {
    *it1 += 1;
    printf("%lu -> %lu\n", it1.GetKey(), *it1);
    ++it1;
  }
  
  return;
}

void DeleteTest() {
  dbg_printf("========== Delete Test ==========\n");
  
  HashTable ht{};

  for(uint64_t i = 0;i < 239;i++) {
    ht.Insert(i, i);
    ht.Insert(i, i + 1);
    ht.Insert(i, i + 2);
    ht.Insert(i, i + 3);
  }
  
  for(int64_t i = 238;i >= 0;i--) {
    ht.DeleteKey((uint64_t)i);
  }
  
  auto it1 = ht.Begin();
  auto it2 = ht.End();
  
  while(it1 != it2) {
    printf("%lu -> %lu\n", it1.GetKey(), *it1);
    ++it1;
  }
}

void DeleteTest2() {
  dbg_printf("========== Delete Test 2 ==========\n");

  HashTable ht{};

  for(uint64_t i = 0;i < 239;i++) {
    ht.Insert(i, i);
    ht.Insert(i, i + 1);
    ht.Insert(i, i + 2);
    ht.Insert(i, i + 3);
  }

  for(int64_t i = 238;i >= 0;i--) {
    auto it = ht.Begin(i);
    ht.Delete(it);
    it = ht.Begin(i);
    ht.Delete(it);
    it = ht.Begin(i);
    ht.Delete(it);
  }

  auto it1 = ht.Begin();
  auto it2 = ht.End();

  while(it1 != it2) {
    printf("%lu -> %lu\n", it1.GetKey(), *it1);
    ++it1;
  }
}

int main() {
  IteratorTest();
  ResizeTest();
  DeleteTest();
  DeleteTest2();

  return 0;
}
