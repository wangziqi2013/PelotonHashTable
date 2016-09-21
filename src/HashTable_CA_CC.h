
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
 *
 * Note that the load factor of this calss defaults to 400% which means that
 * the number of entries when resize is trigger equals 4 * number of slots
 */
template <typename KeyType,
          typename ValueType,
          typename KeyHashFunc = std::hash<KeyType>,
          typename KeyEqualityChecker = std::equal_to<KeyType>,
          typename LoadFactorCalculator = LoadFactorPercent<400>>
class HashTable_CA_CC {
 private:
   
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
    
    /*
     * Constructor - This is used during table initialization for
     *               the dummy entry
     */
    HashEntry() {}
    
    /*
     * Constructor - This one does not initialize next pointer
     */
    HashEntry(uint64_t p_hash_value,
              const KeyType &key,
              const ValueType &value) :
      hash_value{p_hash_value},
      kv_pair{key, value}
    {}
  };
  
  // This is an array holding HashEntry * as the head of a collision chain
  HashEntry **entry_p_list_p;
  
  // This is used as a dummy entry, which is the previous element for any
  // new element inserted into the hash table
  // The next pointer should be initialized to nullptr to mark the last
  // element
  HashEntry dummy_entry;
  
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
   * InsertIntoSlot() - Inserts a node into a certain slot
   *
   * This function distinguishes two cases. If the slot has already been
   * been occupied by some existing entries, then it simply inserts the
   * new entry after the one currently pointed to; Otherwise it installs
   * the new entry after the dummy entry, and redirects the slot that currently
   * points to the next element of the dummy to point to the new element
   *
   * Note that this function does not increase entry count since it is also
   * called by the resize routine
   */
  void InsertIntoSlot(HashEntry *entry_p, uint64_t index) {
    assert(index < slot_count);
    
    HashEntry *p = entry_p_list_p[index];
    
    // This entry has not yet been inserted into before
    // i.e. there is no next element in the hash chain
    if(p == nullptr) {
      // Since the slot was empty just assign the entry to the slot
      entry_p_list_p[index] = entry_p;
      
      // And then let the previous first entry to be the second entry
      // i.e. pointed to by entry_p
      entry_p->next_p = dummy_entry.next_p;
      
      // If the hash table is empty then just insert it without
      // redirecting the slot pointing to the element after dummy_entry
      if(dummy_entry.next_p == nullptr) {
        dummy_entry.next_p = entry_p;
        entry_p->next_p = nullptr;
        
        entry_p_list_p[index] = entry_p;
        
        return;
      }
      
      // We cuold only do this if we know the dummy entry has a
      // next entry record, o.w. segment fault
      uint64_t prev_index = dummy_entry.next_p->hash_value & index_mask;
      
      // It must be reflecsive (i.e. it must be the first element of the slot
      // derived from its hash value; otherwise no other elements are before
      // it and therefore the first element pointed to by the slot)
      assert(entry_p_list_p[prev_index] == dummy_entry.next_p);
      
      // Redirect the slot to point to the new entry
      entry_p_list_p[prev_index] = entry_p;
      
      // Make it the first entry
      dummy_entry.next_p = entry_p;
      
      return;
    }
    
    // In the case where the slot already has element, we just insert
    // the entry as the new first element after the slot's pointer
    
    // Note that here we do not modify entry_p_list_p[index] itself but rather
    // the next element is changed
    entry_p->next_p = p;
    entry_p_list_p[index]->next_p = entry_p;
    
    return;
  }
   
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
    
    // We could free it here right now since we traverse the linked
    // list rather than using this array
    delete[] entry_p_list_p;
    
    slot_count <<= 1;
    index_mask = slot_count - 1;
    
    // Compute the new slot count after updating it
    resize_threshold = lfc(slot_count);
    
    // Allocate a new chunk of memory to hold collision chains
    entry_p_list_p = new HashEntry*[slot_count];
    assert(entry_p_list_p != nullptr);
    
    // Initialize it with nullptr
    memset(entry_p_list_p, 0x0, sizeof(void *) * slot_count);
    
    // Keep this as the starting point of resizing
    HashEntry *entry_p = dummy_entry.next_p;
    
    // Set this as if we are inserting into a fresh new hash table
    dummy_entry.next_p = nullptr;
    
    while(entry_p != nullptr) {
      // Mask it with the new index mask
      uint64_t new_index = entry_p->hash_value & index_mask;
      
      InsertIntoSlot(entry_p, new_index);
      
      // Go to the entry in current valid slots until we have reached
      // to the end
      entry_p = entry_p->next_p;
    }
    
    return;
  }
  
 public:

  /*
   * Constructor
   */
  HashTable_CA_CC(uint64_t p_slot_count = INIT_SLOT_COUNT,
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
    
    // This will be the next_p of the first inserted entry
    dummy_entry.next_p = nullptr;
    
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
  ~HashTable_CA_CC() {
    HashEntry *entry_p = dummy_entry.next_p;
    
    while(entry_p != nullptr) {
      // Save the next pointer first
      HashEntry *temp = entry_p->next_p;
      
      // Then free the entry
      delete entry_p;
      
      // Use this to continue looping
      entry_p = temp;
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
    
    HashEntry *entry_p = \
      new HashEntry{hash_value, key, value};
    assert(entry_p != nullptr);
    
    // Insert into the global linked list; This does not increase
    // entry count
    InsertIntoSlot(entry_p, index);
    
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

    // Special case: there is no entry for the key,
    // i.e. key does not exist
    if(entry_p == nullptr) {
      return;
    }

    // Then loop through the collision chain and check hash value
    // as well as key to find values associated with it
    do {
      entry_p = entry_p->next_p;
      
      if(key_eq_obj(key, entry_p->kv_pair.first) == true) {
        cb(entry_p->kv_pair);
      }
    } while(entry_p != nullptr && \
            entry_p->hash_value == hash_value);
    
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
  };

};

}
}
