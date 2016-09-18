
#pragma once

#include <cstdio>
#include <cassert>
#include <utility>
#include <functional>
#include <type_traits>
#include <cstring>

namespace peloton {
namespace index {

#include "common.h"

/*
 * class HashTable_CA_CC - Hash table implementation that is closed addressing
 *                         and uses collision chain for conflict resolution
 *
 * The CA_CC design has the following characteristics:
 *
 *   1. Each Insert() call only takes constant time for finding the slot
 *      and allocting the new entry
 *   2. Search operation might take potentially O(n) to proceed since if all
 *      keys are hashed to a single slot then we need to traverse them all
 *      However this is even a problem for open addressing
 *   3. Malloc() overhead is larger since every Insert() calls malloc; also
 *      memory menagament overhead is large if key and value are relatively
 *      small
 *   4. Pointer chasing overhead is larger since entries are connected
 *      by pointers. However for a good hash function the length of the
 *      collision chain should be short
 *   5. Cache performance is worse since pointer chasing reduces locality
 */
template <typename KeyType,
          typename ValueType,
          typename KeyHashFunc = std::hash<KeyType>,
          typename KeyEqualityChecker = std::equal_to<KeyType>>
class HashTable_CA_CC {
 private:
  // If collision chain is longer than this then we double the size of the
  // hash table and rehash
  // (It was 128 in Peloton LLVM codegen hash table!!!)
  static constexpr int DEFAULT_CC_MAX_LENGTH = 32;
  
  /*
   * class HashEntry() - The hash entry for holding key and value
   *
   * Note that for an optimal use of the data layout, we put hash value and
   * next element pointer together, since if key and value requires even
   * broader aligment than uint64_t and pointer then this results in a slightly
   * more compact layout
   */
  class HashEntry {
   private:
    // Hash value for fast objecy comparison
    uint64_t hash_value;
    
    // Pointer to the next entry in the collision chain
    HashEntry *next_p;
    
    KeyType key;
    ValueType value;
  };
};

}
}
