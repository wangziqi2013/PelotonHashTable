
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
template <typename KeyType, typename ValueType>
class HashTable_LF_SCC {
  
  /*
   * class HashEntry - Hash table entry, and container for key and value
   */
  class HashEntry {
   private:
    KeyType key;
    ValueType value;
    HashEntry *next_p;
    
   public:
     
    /*
     * Constructor
     */
    HashEntry(const KeyType &p_key, 
              const ValueType &p_value,
              HashEntry *p_next_p) :
      key{p_key},
      value{p_value},
      next_p{p_next_p} 
    {}
    
    /*
     * GetNext() - Returns a reference to the next pointer
     *
     * The next_p pointer could be either read or written using this method
     */
    HashEntry *&GetNext() {
      return next_p;
    }
  };
  
 private:
  
  // The size of the directory array
  size_t dir_size;
  // This is the fixed-length directory array that could only be read/insert
  // in a lock-free manner. Rsizing must be conducted mutual exclusively
  std::atomic<HashEntry *> dir_p;

 public:

  /*
   * Constructor
   */
  HashTable_LF_SCC(size_t size) :
    dir_size{size},
    dir_p{new std::atomic<HashEntry *>[size]} {
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
};
