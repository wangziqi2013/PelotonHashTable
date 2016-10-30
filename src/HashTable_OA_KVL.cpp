
#include "HashTable_OA_KVL.h"

namespace peloton {
namespace index {

void *aligned_malloc_64(size_t sz) {
  void *ret;
  int mem_ret = posix_memalign(&ret, 64, sz);
  assert(mem_ret == 0);
  (void)mem_ret;

  return ret;
}

  
} // namespace index
} // namespace peloton
