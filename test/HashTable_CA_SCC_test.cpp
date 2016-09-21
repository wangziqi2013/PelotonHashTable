
#include "../src/HashTable_CA_SCC.h"

using namespace peloton;
using namespace index;

using HashTable = HashTable_CA_SCC<uint64_t, uint64_t, SimpleInt64Hasher>;

void BasicTest() {
  HashTable ht{30};
  
  for(uint64_t i = 0;i < 1000;i++) {
    ht.Insert(i, i);
  }
  
  for(uint64_t i = 0;i < 1000;i++) {
    std::vector<uint64_t> v{};
    
    ht.GetValue(i, &v);
    assert(v.size() == 1);
    
    dbg_printf("%lu -> %lu\n", i, v[0]);
    assert(i == v[0]);
    
    v.clear();
  }
  
  return;
}

int main() {
  BasicTest();
  
  return 0;
}
