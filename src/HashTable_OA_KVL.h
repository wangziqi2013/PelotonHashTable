
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
  
  /*
   * class KeyValueList - The key value list for holding hash table value
   *                      overflows
   *
   * If one key is mapped to more than one values then we should change the
   * strategy that values are stored, and use this class as an overflow buffer
   * to compactly store all values
   */
  class KeyValueList {

  };
  
  /*
   * class HashEntry - The hash table entry whose array is maintained by the
   *                   hash table
   */
  class HashEntry {
   public:

    /*
     * enum class StatusCode - Describes the status that a HashEntry could be
     *                         in at a time
     *
     * This status code will also share memory with a (<=) 64 bit pointer.
     * Since the pointer could also be 32 bits, this variable should be used
     * for initializing memory
     */
    enum class StatusCode : uint64_t {
      FREE = 0,
      DELETED = 1,
      SINGLE_VALUE = 2,
      // This is a sentinel value that are compared against for >=
      // and if this condition is true then this entry holds multiple
      // values in the list
      MULTIPLE_VALUES = 3,
    };
    
    /*
     * union - This union is made anonymous to expose its members to the
     *         outer scope
     *
     * It works best with 64 bit pointer width, since that case this union is
     * fully utilized. In the case that a pointer is < 64 bit status code
     * shall remain 64 bit
     */
    union {
      StatusCode status;
      KeyValueList *kv_p;
    };
  };
};

}
}
