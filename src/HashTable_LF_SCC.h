
#pragma once

#include "common.h"
#include <atomic>

/*
 * class HashTable_LF_SCC - Hash table implementation with lock-free update
 *                          and read
 *
 * This implementation uses lock-free programming paradigm, in a sense that
 * updates and removes to linked list under each bucket's collision chain
 * is done in a lock-free manner, i.e. through CAS instruction. 
 *
 * This implementation does not contain a resize() operation, favoring 
 * simplicity rather than completeness. It is required that the directory
 * array being declared to be large enough to maintain a reasonable load
 * factor (which implies the number of entries shouls be known beforehead) 
 */
template <typename KeyType, 
          typename ValueType,
          typename KeyEqualityChecker=std::equal_to<KeyType>,
          typename KeyHashFunc=std::hash<KeyType>,
          typename ValueEqualityChecker=std::equal_to<ValueType>>
class HashTable_LF_SCC {
  
  /*
   * class HashEntry - Hash table entry, and container for key and value
   */
  class HashEntry {
   private:
    KeyType key;
    ValueType value;
    
    std::atomic<HashEntry *> next_p;
    
   public:
     
    /*
     * Constructor
     */
    HashEntry(const KeyType &p_key, 
              const ValueType &p_value,
              HashEntry *p_next_p) :
      key{p_key},
      value{p_value} {
      next_p.store(p_next_p);  
    }
    
    /*
     * GetNext() - Returns a reference to the next pointer
     *
     * The next_p pointer could be either read or written using this method
     */
    std::atomic<HashEntry *> &GetNext() {
      return next_p;
    }
  };
  
 private:
  
  // The size of the directory array
  size_t dir_size;
  // This is the fixed-length directory array that could only be read/insert
  // in a lock-free manner. Rsizing must be conducted mutual exclusively
  std::atomic<HashEntry *> dir_p;
  // Compares whether two keys are equal
  KeyEqualityChecker key_eq_obj;
  // This is a functor that hashes keys into size_t values
  KeyHashFunc key_hash_obj;
  // Compares whether two values are equal
  ValueEqualityChecker value_eq_obj;

 public:

  /*
   * Constructor
   */
  HashTable_LF_SCC(size_t size,
                   const KeyEqualityChecker &p_key_eq_obj=KeyEqualityChecker{},
                   const KeyHashFunc &p_key_hash_obj=KeyHashFunc{},
                   const ValueEqualityChecker &p_value_eq_obj=ValueEqualityChecker{}) :
    dir_size{size},
    dir_p{new std::atomic<HashEntry *>[size]},
    key_eq_obj{p_key_eq_obj},
    key_hash_obj{p_key_hash_obj},
    value_eq_obj{p_value_eq_obj} {
    assert(dir_p != nullptr);
    // Make sure the size of atomic variable equals the size of a raw pointer
    assert(sizeof(std::atomic<HashEntry *>) == sizeof(void *));
    
    // Set the memory region into 0x00 (nullptr) for later use
    // Note that here we do not use any special memory ordering which
    // might introduce problem. So let;s put a memory barrier to
    // wait for writes to complete before the constructor exits
    memset(dir_p, 0x00, sizeof(std::atomic<HashEntry *>) * size);
    
    // Make sure all store instruction reach the cache before
    // constructor exits
    // Is it really necessary? - As long as pthread library issue memory barrier
    // before thread switch this is useless...
    asm volatile ("mfence" ::: "memory");
    
    return;
  }
  
  /*
   * Insert() - Inserts into the hash table
   *
   * Note that we do not check for key-value consistency. Insert operation
   * only takes place at the head of a linked list, and if there is duplicated
   * keys on Delete() they should be deleted multiple times
   */
  void Insert(const KeyType &key, const ValueType &value) {
    size_t hash = key_hash_obj(key);
    
    // This is the value of the head of the linked list
    HashEntry *head_p = dir_p[hash].load();
    
    HashEntry *entry_p = new HashEntry{key, value, head_p};
    assert(entry_p != nullptr);
    
    while(1) {
      // Try to CAS the new entry at the head of the linked list
      ret = dir_p[hash].compare_exchange_strong(head_p, entry_p);
      if(ret == true) {
        break; 
      }
      
      // Then readjust the header node and retry
      // Note that if CAS returns false then the most up to date value
      // is automatically loaded
      entry_p->GetNext().store(head_p);
    } 
    
    return;
  }
  
  /*
   * Delete() - Deletes a key-value pair from the hash table, if it exists
   *
   * This function returns false if the delete does not happen for absent
   * key-value pair
   *
   * If the function returns true then exactly one entry is deleted, even if
   * there are multiple matches at that moment
   */
  bool Delete(const KeyType &key, const ValueType &value) {
    size_t hash = key_hash_obj(key);
    
    // This is the value of the head of the linked list
    HashEntry *prev_next_p = dir_p + hash;
    HashEntry *next_p = prev_next_p->load();
    
    while(1) {
      if(key_eq_obj(key, next_p->GetKey() == true) && \
         value_eq_obj(value, next_p->GetValue()) == true) {
        //HashEntry *new_next_p = next_p->   
      }
    }
  }
  
  
};
