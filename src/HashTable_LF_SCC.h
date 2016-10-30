
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
class HashTable_LF_SCC {
 private:
  
};
