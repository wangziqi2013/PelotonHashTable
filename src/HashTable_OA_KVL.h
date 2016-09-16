
#pragma once

#include <utility>
#include <functional>

namespace peloton {
namespace index {

/*
 * class HashTable_OA_KVL - Open addressing hash table for storing key-value
 *                          pairs that tses Key Value List for dealing with
 *                          multiple values for a single key
 *
 * This hash table uses open addressing to resolve conflicts, and use an
 * overflow buffer (i.e. Key Value List) to resolve multiple values for a
 * single keys
 *
 * Trade-offs for choosing this hash table design:
 *
 *   1. Open addressing design and inlined data storage makes it cache friendly
 *      but on the other hand, since the load factor must be maintained at a
 *      reasonably level, more memory are allocated for efficiency
 *
 *   2. KeyValueList makes iterating over values for a given key fast, but if
 *      there is not much key duplicates then this design has the disadvantage
 *      of requiring out-of-band memory and maintenance overhead for inserting
 *      keys
 *
 */
template <typename KeyType,
          typename ValueType,
          typename KeyHashFunc = std::hash<KeyType>,
          typename KeyEqualityChecker = std::equal_to<KeyType>>
class HashTable_OA_KVL {
 private:
  // This is the type we use for storing key value pair inside HashEntry
  using KeyValueType = std::pair<KeyType, ValueType>;
};

}
}
