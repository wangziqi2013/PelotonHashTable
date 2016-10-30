
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
    
    void SetNext(HashEntry *p_next_p) {
      next_p = p_next_p;
      
      return;
    }
  };
  
 private:
  
};
