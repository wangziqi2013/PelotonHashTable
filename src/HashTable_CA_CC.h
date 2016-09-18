
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
  
  // The size of a typical page
  static constexpr uint64_t PAGE_SIZE = 4096;
  
  // Let the first array fill one entire page
  static constexpr uint64_t INIT_SLOT_COUNT = PAGE_SIZE / sizeof(void *);
  /*
   * class HashEntry() - The hash entry for holding key and value
   *
   * Note that for an optimal use of the data layout, we put hash value and
   * next element pointer together, since if key and value requires even
   * broader aligment than uint64_t and pointer then this results in a slightly
   * more compact layout
   */
  class HashEntry {
    friend class HashTable_CA_CC;
    
   private:
    // Hash value for fast objecy comparison
    uint64_t hash_value;
    
    // Pointer to the next entry in the collision chain
    HashEntry *next_p;
    
    // We put them into a pair to make iterator easier to implement
    std::pair<KeyType, ValueType> kv_pair;

   public:
    
    /*
     * Constructor
     */
    HashEntry(uint64_t p_hash_value,
              HashEntry *p_next_p,
              const KeyType &key,
              const ValueType *value) :
      hash_value{p_hash_value},
      next_p{p_next_p},
      kv_pair{key, value}
    {}
  };
  
  // This is an array holding HashEntry * as the head of a collision chain
  HashEntry **entry_p_list_p;
  
  // Used to mask off insignificant bits for computing the index
  uint64_t index_mask;
  
  // Size of the entry pointer list
  uint64_t slot_count;
  
  // Number of HashEntry in this hash table
  uint64_t entry_count;
  
  // This is the length of the chain
  // If the length of any collision chain exceeds this threshold then
  // a rehash will be scheduled
  // Note that this will not be changed even if the size of the array changes
  int resize_threshold;
  
  // Specialized function for computing hash and comparison
  KeyHashFunc key_hash_obj;
  KeyEualityChecker key_eq_obj;
  
 private:
   
  /*
   * Resize() - Double the size of the array and scatter elements
   *            into their new position
   */
  void Resize() {
    // Preserver these two values
    uint64_t old_slot_count = slot_count;
    HashEntry **old_entry_p_list_p = entry_p_list_p;
    
    slot_count <<= 1;
    index_mask = slot_count - 1;
    
    // Allocate a new chunk of memory to hold collision chains
    entry_p_list_p = new HashEntry*[slot_count];
    memset(entry_p_list_p, 0x0, sizeof(void *) * slot_count);
    
    // Iterate through all slots first
    for(uint64_t i = 0;i < old_slot_count;i++) {
      // This points to the first element of the collision chain
      HashEntry *entry_p = old_entry_p_list_p[i];
      
      while(entry_p != nullptr) {
        // Mask it with the new index mask
        uint64_t new_index = entry_p->hash_value & index_mask;
        
        // Save the next pointer first
        HashEntry *temp = entry_p->next_p;
        
        // Link it to the new slot's chain
        entry_p->next_p = entry_p_list_p[new_index];
        // Let the new slot point to it also
        entry_p_list_p[new_index] = entry_p;
        
        // And then use this pointe to continue the loop
        entry_p = temp;
      }
    }
    
    // Free the memory for old entry pointer list
    delete[] old_entry_p_list_p;
    
    return;
  }
  
 public:

  /*
   * Constructor
   */
  HashTable_CA_CC(uint64_t p_slot_count = INIT_SLOT_COUNT,
                  int p_resize_threshold = DEFAULT_CC_MAX_LENGTH,
                  const KeyHashFunc &p_key_hash_obj = KeyHashFunc{},
                  const KeyEqualityChecker &p_key_eq_obj = KeyEqualityChecker{}) :
    slot_count{p_slot_count},
    entry_count{0},
    resize_threshold{p_resize_threshold},
    key_hash_obj{p_key_hash_obj},
    key_eq_obj{p_key_eq_obj} {
    // First round it up to power of 2
    int leading_zero = __builtin_clzl(slot_count);
    int effective_bits = 64 - leading_zero;

    // It has a 1 bit on the highest bit
    slot_count = 0x0000000000000001 << effective_bits;
    index_mask = entry_count - 1;
    
    entry_p_list_p = new HashEntry*[slot_count];
    memset(entry_p_list_p, 0x0, sizeof(void *) * slot_count);
    
    return;
  }
  
  /*
   * Destructor - Frees the array and all entries
   */
  ~HashTable_CA_CC() {
    for(uint64_t i = 0;i < slot_count;i++) {
      HashEntry *entry_p = entry_p_list_p[i];
      
      while(entry_p != nullptr) {
        // Save the next pointer first
        HashEntry *temp = entry->next_p;
        
        // Then free the entry
        delete entry_p;
        
        // Use this to continue looping
        entry_p = temp;
      }
    }
    
    // Also free the pointer array
    delete[] entry_p_list_p;
    
    return;
  }

  /*
   * Insert() - Adds a key value pair into the table
   *
   * This operation invalidates existing iterators in case of a rehash
   */
  void Insert(const KeyType &key, const ValueType &value) {
    uint64_t hash_value = key_hash_obj(key);
    uint64_t index = index_mask & hash_value;
    
    // This should be done for every insert
    entry_count++;
    
    // Update the pointer of the slot by allocating a new entry
    entry_p_list_p[index] = \
      new HashEntry{hash_value, entry_p_list_p[index], key, value};
    assert(entry_p_list_p[index] != nullptr);
    
    return;
  }
  
  /*
   * GetValue() - For a given key, invoke the given call back on the key
   *              value pair associated with the entry
   */
  void GetValue(const KeyType &key,
                std::function<void(const std::pair<KeyType, ValueType> &)> cb) {
    uint64_t hash_value = key_hash_obj(key);
    uint64_t index = index_mask & hash_value;

    HashEntry *entry_p = entry_p_list_p[index];

    int chain_length = 0;

    // Then loop through the collision chain and check hash value
    // as well as key to find values associated with it
    while(entry_p != nullptr) {
      if(hash_value == entry_p->hash_value) {
        if(key_eq_obj(key, entry_p->kv_pair.first) == true) {
          cb(entry_p->kv_pair);
        }
      }

      entry_p = entry_p->next_p;
      chain_length++;
    }
    
    // If the length of the delta chain is greater than or equal to the
    // threshold
    if(chain_length >= resize_threshold) {
      Resize();
    }
    
    return;
  }
  
  /*
   * GetValue() - Return all value elements in a vector
   */
  void GetValue(const KeyType &key, std::vector<ValueType> *value_list_p) {
    // Call the GetValue() with a call back defined as lambda function
    // that pushes each individual value into the list
    GetValue(key,
             [value_list_p](const std::pair<KeyType, ValueType> &kv_pair) {
               value_list_p->push_back(kv_pair.second);
               
               return;
             });
    
    return;
  }
  
 private:
   
  /*
   * class Iterator - Iterates through the hash table
   */
  class Iterator {
    friend class HashTable_CC_CA;
    
   private:
    HashEntry *entry_p;
    uint64_t slot_index;
  };

};

}
}
