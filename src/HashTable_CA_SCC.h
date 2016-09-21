
#pragma once

#include <cstdio>
#include <cassert>
#include <utility>
#include <functional>
#include <cstring>
#include <vector>

namespace peloton {
namespace index {

#include "common.h"

/*
 * class HashTable_CA_SCC - Close addressing, simple collision chain hash table
 *
 * This design is very similar to HashTable_CA_CC, except that:
 *
 *   1. In CA_CC all HashEntry objects are connected together forming a singly
 *      linked list, which facilitates constant time iteration. In CA_SCC
 *      collision chains are maintained in a per slot basis, and are not
 *      connected together across slots
 *   2. CA_SCC favors fast lookup since each lookup does not have to check
 *      next entry's hash value to decide whether we are still on the
 *      collision chain of the current hash value
 *   3. Iteration on CA_SCC needs to iterates all slots to check whether there
 *      is a collision chain for that slot, rather than simply do a linked
 *      list traversal.
 *   4. Iterator implementation is more complicated and requires more than the
 *      size of an ordinary pointer
 */
template <typename KeyType,
          typename ValueType,
          typename KeyHashFunc = std::hash<KeyType>,
          typename KeyEqualityChecker = std::equal_to<KeyType>,
          typename LoadFactorCalculator = LoadFactorPercent<400>>
class HashTable_CA_SCC {
 private:
   
  // The size of a typical page
  static constexpr uint64_t PAGE_SIZE = 4096;
  
  // Let the first array fill one entire page
  static constexpr uint64_t INIT_SLOT_COUNT = PAGE_SIZE / sizeof(void *);
  
  /*
   * class HashEntry - The hash entry for holding key and value
   *
   * Note that for an optimal use of the data layout, we put hash value and
   * next element pointer together, since if key and value requires even
   * broader aligment than uint64_t and pointer then this results in a slightly
   * more compact layout
   */
  class HashEntry {
    friend class HashTable_CA_SCC;
    
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
              const ValueType &value) :
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
  
  // Number of HashEntry in this hash table (which could be larger than
  // slot count)
  uint64_t entry_count;
  
  // This is the totoal number of entries for the current
  // table size
  // Note that this is different from the load factor of an open addressing
  // hash table in a sense that load factor here should normally be > 1.0
  uint64_t resize_threshold;
  
  // Specialized function for computing hash and comparison
  KeyHashFunc key_hash_obj;
  KeyEqualityChecker key_eq_obj;
  LoadFactorCalculator lfc;
  
 private:
   
  /*
   * Resize() - Double the size of the array and scatter elements
   *            into their new position
   *
   * This function updates slot count, resize threshold and index mask
   * and also the pointer to the HashEntry * array
   */
  void Resize() {
    // Entry count is the number of HashEntry object
    assert(entry_count == resize_threshold);
    
    // Save old pointers in order to traverse using them
    HashEntry **old_p = entry_p_list_p;
    uint64_t old_slot_count = slot_count;
    
    slot_count <<= 1;
    index_mask = slot_count - 1;
    
    // Compute the new slot count after updating it
    resize_threshold = lfc(slot_count);
    
    // Allocate a new chunk of memory to hold collision chains
    entry_p_list_p = new HashEntry*[slot_count];
    assert(entry_p_list_p != nullptr);
    
    // Initialize it with nullptr
    memset(entry_p_list_p, 0x0, sizeof(void *) * slot_count);
    
    for(uint64_t i = 0;i < old_slot_count;i++) {
      // Make it the starting point of traversing each slot
      HashEntry *entry_p = old_p[i];
      
      while(entry_p != nullptr) {
        // Mask it with the new index mask
        uint64_t new_index = entry_p->hash_value & index_mask;
        
        assert(new_index < slot_count);

        // Need to save it first since its next_p will be changed
        HashEntry *next_p = entry_p->next_p;

        // Hook the entry onto the new slot
        // Also Note that we changed entry_p->next_p here
        entry_p->next_p = entry_p_list_p[new_index];
        entry_p_list_p[new_index] = entry_p;

        // Go to the entry in current valid slots until we have reached
        // to the end
        entry_p = next_p;
      }
    }
    
    // Must remove it after traversing all slots
    delete[] old_p;
    
    return;
  }
  
 public:

  /*
   * Constructor
   */
  HashTable_CA_SCC(uint64_t p_slot_count = INIT_SLOT_COUNT,
                   const KeyHashFunc &p_key_hash_obj = KeyHashFunc{},
                   const KeyEqualityChecker &p_key_eq_obj = KeyEqualityChecker{},
                   const LoadFactorCalculator &p_lfc = LoadFactorCalculator{}) :
    slot_count{p_slot_count},
    entry_count{0},
    key_hash_obj{p_key_hash_obj},
    key_eq_obj{p_key_eq_obj},
    lfc{p_lfc} {
    // First round it up to power of 2
    int leading_zero = __builtin_clzl(slot_count);
    int effective_bits = 64 - leading_zero;

    // It has a 1 bit on the highest bit
    slot_count = 0x0000000000000001 << effective_bits;
    index_mask = slot_count - 1;
    
    // If slot count proposed by the caller is a power of 2 then
    // we have 1 more bit shifted, and then just shift it back
    if((slot_count >> 1) == slot_count) {
      slot_count >>= 1;
      index_mask >>= 1;
    }
    
    // This is the number of entries required to perform resize
    resize_threshold = lfc(slot_count);
    
    entry_p_list_p = new HashEntry*[slot_count];
    memset(entry_p_list_p, 0x0, sizeof(void *) * slot_count);
    
    dbg_printf("Slot count = %lu\n", slot_count);
    
    return;
  }
  
  /*
   * Destructor - Frees the array and all entries
   *
   * Note that we do not have to traverse each slot since all entries are
   * linked together as a singly linked list
   */
  ~HashTable_CA_SCC() {
    for(uint64_t i = 0;i < slot_count;i++) {
      // This is the starting point of deleting nodes in the hash table
      HashEntry *entry_p = entry_p_list_p[i];
      
      while(entry_p != nullptr) {
        // Save the next pointer first since after delete the content no
        // longer remain valid
        HashEntry *temp = entry_p->next_p;

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
   * This operation does not invalidate any iterator on existing entries
   */
  void Insert(const KeyType &key, const ValueType &value) {
    // Let's resize
    if(entry_count == resize_threshold) {
      Resize();
      
      // Make sure that after each resize the entry count is always
      // less than the resize threshold
      assert(entry_count < resize_threshold);
    }
    
    uint64_t hash_value = key_hash_obj(key);
    uint64_t index = index_mask & hash_value;
    
    // We do not initialize its next_p since it will not be relied on
    HashEntry *entry_p = \
      new HashEntry{hash_value, entry_p_list_p[index], key, value};
    assert(entry_p != nullptr);
    
    // Assign the entry as the first element of the collision chain
    entry_p_list_p[index] = entry_p;
    
    // Do not forget this
    entry_count++;
    
    return;
  }
  
  /*
   * GetValue() - For a given key, invoke the given call back on the key
   *              value pair associated with the entry
   *
   * Note that this function might cause a resize if during the traversal
   * the delta chain is larger
   */
  void GetValue(const KeyType &key,
                std::function<void(const std::pair<KeyType, ValueType> &)> cb) {
    uint64_t hash_value = key_hash_obj(key);
    uint64_t index = index_mask & hash_value;

    HashEntry *entry_p = entry_p_list_p[index];

    // Then loop through the collision chain and check hash value
    // as well as key to find values associated with it
    while(entry_p != nullptr) {
            
      if(key_eq_obj(key, entry_p->kv_pair.first) == true) {
        cb(entry_p->kv_pair);
      }
      
      // Immediately go to the next element
      entry_p = entry_p->next_p;
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

};

}
}
