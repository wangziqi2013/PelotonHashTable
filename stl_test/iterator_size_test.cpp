
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <cstdio>

int main() {
  printf("sizeof(unordered_map::iterator) = %lu\n",
         sizeof(std::unordered_map<uint64_t, uint64_t>::iterator));
  printf("sizeof(map::iterator) = %lu\n",
         sizeof(std::map<uint64_t, uint64_t>::iterator));
  printf("sizeof(unordered_set::iterator) = %lu\n",
         sizeof(std::unordered_set<uint64_t>::iterator));
         
  // Conslusion: All iterators are of size 8 which does not carry any
  // extra state other than a raw pointer
         
  return 0;
}
