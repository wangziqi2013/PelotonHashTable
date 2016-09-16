
#pragma once

#include <cassert>
#include <utility>
#include <functional>

namespace peloton {
namespace index {

#define DEBUG_PRINT

#ifdef DEBUG_PRINT

#define dbg_printf(fmt, ...)                              \
  do {                                                    \
    fprintf(stderr, "%-24s: " fmt, __FUNCTION__, ##__VA_ARGS__); \
    fflush(stdout);                                       \
  } while (0);

#else

static void dummy(const char*, ...) {}

#define dbg_printf(fmt, ...)   \
  do {                         \
    dummy(fmt, ##__VA_ARGS__); \
  } while (0);

#endif

// I copied this from Linux kernel code to favor branch prediction unit on CPU
// if there is one
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/*
 * class LoadFactorHalfFull - Compute load factor as 0.5
 *
 * We choose to implement the load factor as a call back function rather than
 * a constant in order to let the user choose a more flexible strategy to
 * adapt the table to varying workloads
 */
class LoadFactorHalfFull {
 public:
   
  /*
   * operator() - Computes the resize threshold given the current table size
   *
   * This will be only called during initialization and table resizing
   */
  uint64_t operator()(uint64_t table_size) {
    return table_size >> 1;
  }
};

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
          typename KeyEqualityChecker = std::equal_to<KeyType>,
          typename LoadFactorCalculator = LoadFactorHalfFull>
class HashTable_OA_KVL {
 private:
  // This is the minimum entry count
  static constexpr uint64_t MINIMUM_ENTRY_COUNT = 32;
  static constexpr uint64_t PAGE_SIZE = 4096;
  
 public:
   
  /*
   * class SimpleInt64Hasher - Simple hash function that hashes uint64_t
   *                           into a value that are distributed evenly
   *                           in the 0 and MAX interval
   *
   * Note that for an open addressing hash table, simply do a reflexive mapping
   * is not sufficient, since integer keys tend to group together in a very
   * narrow interval, using the ineteger itself as hashed value might cause
   * aggregation
   */
  class SimpleInt64Hasher {
   public:
    uint64_t operator()(uint64_t value) {
      //
      // The following code segment is copied from MurmurHash3, and is used
      // as an answer on the Internet:
      // http://stackoverflow.com/questions/5085915/what-is-the-best-hash-
      //   function-for-uint64-t-keys-ranging-from-0-to-its-max-value
      //
      value ^= value >> 33;
      value *= 0xff51afd7ed558ccd;
      value ^= value >> 33;
      value *= 0xc4ceb9fe1a85ec53;
      value ^= value >> 33;
      
      return value;
    }
  };
  
 private:
  
  /*
   * class KeyValueList - The key value list for holding hash table value
   *                      overflows
   *
   * If one key is mapped to more than one values then we should change the
   * strategy that values are stored, and use this class as an overflow buffer
   * to compactly store all values
   *
   * We choose not to use a std::vector since std::vector will cause extra
   * allocation
   */
  class KeyValueList {
   public:
    // Number of value items inside the list
    uint32_t size;
    // The actual capacity allocated to the list
    uint32_t capacity;
    
    // The following elements are
    ValueType data[0];
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
    
    uint64_t hash_value;

    // The inline storage for key-value works best if one key is only
    // mapped to one value
    // However, if one key is mapped to multiple values, we keep the key inline
    // but all values will be stored in the KeyValueList
    KeyType key;
    ValueType value;
    
    /*
     * IsFree() - Returns whether the slot is currently unused
     */
    inline bool IsFree() const {
      return status == StatusCode::FREE;
    }
    
    /*
     * IsDeleted() - Whether the slot is deleted
     */
    inline bool IsDeleted() const {
      return status == StatusCode::DELETED;
    }
    
    /*
     * IsProbeEndForInsert() - Whether it is the end of probing
     *                         for insert operation
     *
     * For insert operations, deleted entry could be inserted into
     * so all status smaller than SINGLE_VALUE is considered as being
     * eligible for insertion
     */
    inline bool IsProbeEndForInsert() const {
      return status < StatusCode::SINGLE_VALUE;
    }
    
    /*
     * IsValidEntry() - Whether the entry has a key and at least one value
     *
     * The trick here is that for all values >= SINGLE_VALUE, they indicate
     * valid entries
     */
    inline bool IsValidEntry() const {
      return status >= StatusCode::SINGLE_VALUE;
    }
    
    /*
     * IsProbeEndForSearch() - Whether it is the end of probing
     *                         for search operation
     *
     * For search operations, deleted entry could not be counted as the
     * end of probing
     */
    inline bool IsProbeEndForSearch() const {
      return IsFree();
    }
    
    /*
     * HasSingleValue() - Whether the entry only has one value
     *
     * This method requires that the entry must have one or more values
     */
    inline bool HasSingleValue() const {
      assert((IsFree() == false) &&
             (IsDeleted() == false));
             
      return status == StatusCode::SINGLE_VALUE;
    }
    
    /*
     * HasKeyValueList() - returns if the entry has a key value list
     *
     * The trick here is that we consider a normal pointer value
     * as larger than any of the defined status code
     */
    inline bool HasKeyValueList() const {
      return status >= StatusCode::MULTIPLE_VALUES;
    }
  };
  
 private:

  ///////////////////////////////////////////////////////////////////
  // Data Member Definition
  ///////////////////////////////////////////////////////////////////
  
  // This is the major data array of the hash table
  HashEntry *entry_list_p;
  
  // The bit mask used to convert hash value into an index value into
  // the hash table
  uint64_t index_mask;
  
  // Number of active elements
  uint64_t active_entry_count;
  
  // Total number of entries
  uint64_t entry_count;
  
  // We compute threshold for next resizing, and cache it here
  uint64_t resize_threshold;
  
  KeyHashFunc key_hash_obj;
  KeyEqualityChecker key_eq_obj;
  LoadFactorCalculator lfc;
  
 private:
  
  /*
   * GetNextEntry() - Get the next entry
   *
   * This function takes care of wrapping back and also branch prediction
   * since wrap back is a rare event, the compiler should have this piece
   * of information, and arrange code intelligently to let the CPU
   * favor the branch that is likely to be execute, i.e. no wrapping back
   *
   * This function should also be inlined such that no actual pointer operation
   * is performed
   */
  inline void GetNextEntry(HashEntry **entry_p_p, uint64_t *index_p) {
    // Increase them first, since usually we do not need a wrap back
    (*index_p)++;
    (*entry_p_p)++;
    
    // If the index is already out of bound
    if(unlikely(*index_p == entry_count)) {
      *index_p = 0;
      *entry_p_p = entry_list_p;
    }
    
    return;
  }
  
  /*
   * ProbeForResize() - Given a hash value, probe it in the array and return
   *                    the first HashEntry pointer that is free
   *
   * Since for resizing we only consider free and non-free slots, and there
   * is no deleted slots, probing is pretty easy
   */
  HashEntry *ProbeForResize(uint64_t hash_value) {
    // Compute the starting point for probing the hash table
    uint64_t index = hash_value & index_mask;
    HashEntry *entry_p = entry_list_p + index;

    // Keep probing until there is a entry that is not free
    while(entry_p->IsFree() == false) {
      GetNextEntry(&entry_p, &index);
    }

    // It could only be a free entry
    return entry_p;
  }
  
  /*
   * ProbeForInsert() - Given a hash value, probe it in the array and return
   *                    the first HashEntry pointer that this hash value's
   *                    key could be inserted into
   *
   * This function returns the entry iff:
   *
   *   1. The entry is deleted
   *   2. The entry is free
   *   3. The entry is neither deleted nor free, but has a key that matches
   *      the given key
   */
  HashEntry *ProbeForInsert(const KeyType &key) {
    // Compute the starting point for probing the hash table
    uint64_t index = key_hash_obj(key) & index_mask;
    HashEntry *entry_p = entry_list_p + index;

    // Keep probing until there is a entry that is not free
    // Since we always assume the table does not become entirely full,
    // a free slot could always be inserted
    while(entry_p->IsProbeEndForInsert() == false) {
      // If we have found the key, then directly return
      if(key_eq_obj(key, entry_p->key) == true) {
        return entry_p;
      }
      
      GetNextEntry(&entry_p, &index);
    }

    // It is either a deleted or free entry
    // which could be determined by only 1 instruction
    return entry_p;
  }
  
  /*
   * ProbeForSearch() - Probe the array to find the entry of given key
   *
   * If the entry is not found then return nullptr. If we are doing an
   * insertion later on then a reprobe is required
   */
  HashEntry *ProbeForSearch(const KeyType &key) {
    // Compute the starting point for probing the hash table
    uint64_t index = key_hash_obj(key) & index_mask;
    HashEntry *entry_p = entry_list_p + index;

    // Keep probing until there is a entry that is not free
    // Since we always assume the table does not become entirely full,
    // a free slot could always be inserted
    while(entry_p->IsProbeEndForSearch() == false) {
      // If we reach here the entry still could be a deleted entry
      // Check for status of deletion first
      if((entry_p->IsDeleted() == false) && \
         (key_eq_obj(key, entry_p->key) == true)) {
        return entry_p;
      }

      GetNextEntry(&entry_p, &index);
    }

    // There is no entry
    return nullptr;
  }
  
  /*
   * Resize() - Double the size of the table, and do a reprobe for every
   *            existing element
   *
   * This function allocates a new array and frees the old array, and calls
   * copy constructor for each valid entry remaining in the old array into
   * the new array
   */
  void Resize() {
    entry_count <<= 1;
    index_mask = entry_count - 1;
    // Use the user provided call back to compute the load factor
    resize_threshold = lfc(resize_threshold);
    
    // Preserve the old entry list and allocate a new one
    HashEntry *old_entry_list_p = entry_list_p;
    entry_list_p = new HashEntry[entry_count];
    assert(entry_list_p != nullptr);
    
    // Use this to iterate through all entries and rehash them into
    // the new array
    uint64_t remaining = active_entry_count;
    HashEntry *entry_p = old_entry_list_p;
    while(remaining > 0) {
      if(entry_p->IsValidEntry() == true) {
        remaining--;
        
        // This is the place where we insert the entry in
        HashEntry *new_entry_p = ProbeForResize(entry_p->hash_value);
        
        // This calls the copy constructor for class HashEntry which is
        // synthesized by the compiler which in turn calls the copy constructor
        // for KeyType and ValueType
        *entry_p = *new_entry_p;
      }
    }
    
    // Free old list to avoid memory leak
    delete[] old_entry_list_p;
    
    return;
  }
  
  /*
   * SetSizeAndMask() - Sets entry count and index mask
   *
   * This is only called for initialization routine, since for later grow
   * of the table we always double the table, so the valus are easier to
   * compute
   */
  void SetSizeAndMask(uint64_t requested_size) {
    int leading_zero = __builtin_clzl(requested_size);
    int effective_bits = 64 - leading_zero;

    // It has a 1 bit on the highest bit
    entry_count = 0x0000000000000001 << effective_bits;
    index_mask = entry_count - 1;

    // If this happens then requested size itself is a power of 2
    // and we just counted more than 1 bit
    if(requested_size == (entry_count >> 1)) {
      entry_count >>= 1;
      index_mask >>= 1;
    }

    return;
  }

  /*
   * GetInitEntryCount() - Returns the initial entry count given a requested
   *                       size of the hash table
   */
  static uint64_t GetInitEntryCount(uint64_t requested_size) {
    if(requested_size < HashTable_OA_KVL::MINIMUM_ENTRY_COUNT) {
      requested_size = HashTable_OA_KVL::MINIMUM_ENTRY_COUNT;
    }

    // Number of elements that could be held by one page
    // If we could hold more items in one page then update the size
    uint64_t proposed_size = HashTable_OA_KVL::PAGE_SIZE / sizeof(HashEntry);
    if(proposed_size > requested_size) {
      requested_size = proposed_size;
    }

    return requested_size;
  }

  /*
   * FreeKeyValueList() - Frees the key value list associated with HashEntry
   *                      if there is one
   *
   * Note that we always use operator new and operator delete for memory
   * allocation in this class
   */
  void FreeKeyValueList() {
    // We use this variable as end of loop condition
    uint64_t remaining = active_entry_count;
    HashEntry *entry_p = entry_list_p;

    // We use this as an optimization, since as long as we have finished
    // iterating through all valid entries
    while(remaining > 0) {
      // The variable counts valid entry
      if(entry_p->IsValidEntry() == true) {
        remaining--;
      }

      // And among all valid entries we only destroy those that have
      // a key value list
      if(entry_p->HasKeyValueList() == true) {
        // Free the pointer
        delete entry_p->kv_p;
      }

      // Always go to the next entry
      entry_p++;
    }

    return;
  }
  
 public:
   
  /*
   * Constructor - Initialize hash table entry and all of its members
   *
   * The constructor takes key comparator, key hash function and load factor
   * calculator as arguments. If not provided then they are defaulted to be
   * the default initialized object from the functor class
   *
   * Also the initial number of entries in the array could be specified by the
   * caller. If not specified then it is either set to the larger one between
   * hardcoded minimum number of entries, or to PAGE_SIZE / sizeof(HashEntry)
   */
  HashTable_OA_KVL(const KeyHashFunc &p_key_hash_obj = KeyHashFunc{},
                   const KeyEqualityChecker &p_key_eq_obj = KeyEqualityChecker{},
                   const LoadFactorCalculator &p_lfc = LoadFactorCalculator{},
                   uint64_t init_entry_count = 0) :
    active_entry_count{0},
    key_hash_obj{p_key_hash_obj},
    key_eq_obj{p_key_eq_obj},
    lfc{p_lfc} {
    // First initialize this variable to make it as reasonable as possible
    init_entry_count = GetInitEntryCount(init_entry_count);
                       
    // First round it up to a power of 2 and then compute size and mask
    SetSizeAndMask(init_entry_count);
    
    // Set the threshold by setting the load factor
    resize_threshold = lfc(entry_count);
    
    // If KeyType and ValueType have non-trivial constructors then this might
    // take very long time
    // In the future we might want to modify this to let the constructor be
    // called only on demand, i.e. when the entry is inserted
    // and destructor called when the entry is deleted
    entry_list_p = new HashEntry[entry_count];
    assert(entry_list_p != nullptr);
    
    dbg_printf("Hash table size = %lu\n", entry_count);
    dbg_printf("Resize threshold = %lu\n", resize_threshold);
    
    return;
  }
  
  /*
   * Destructor - Frees all memory, including HashEntry array and KeyValueList
   */
  ~HashTable_OA_KVL() {
    // Free all key value list first
    FreeKeyValueList();
    
    // Free the array
    assert(entry_list_p);
    delete[] entry_list_p;
    
    return;
  }
  
  void Insert(const KeyType &key, const ValueType &value) {
    
  }
  
  ValueType *GetValuePointer(const KeyType &key) {
    if(entry_count == resize_threshold) {
      Resize();
      assert(entry_count < resize_threshold);
    }
    
  }
};

}
}
