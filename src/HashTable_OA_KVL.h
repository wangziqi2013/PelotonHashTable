
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

/*
 * class LoadFactorHalfFull - Compute load factor as 0.5
 */
class LoadFactorHalfFull {
 public:
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
     */
    inline bool IsProbeEndForInsert() const {
      return IsFree() || IsDeleted();
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
   * Note that we always use malloc and free for memory allocation in this
   * class, so free() should be used
   */
  void FreeKeyValueList() {
    uint64_t remaining = active_entry_count;
    HashEntry *entry_p = entry_list_p;
    
    // We use this as an optimization, since as long as we have finished
    // iterating through all valid entries
    while(remaining > 0) {
      if(entry_p->HasKeyValueList() == true) {
        remaining--;
        
        // Free the pointer
        free(entry_p->kv_p);
      }
      
      // Always go to the next entry
      entry_p++;
    }
    
    return;
  }
  
 public:
   
  /*
   * Constructor - Initialize hash table entry and all of its members
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
    
    // We do not call new to avoid calling the constructor for each key and
    // value
    // Note that sizeof() takes padding into consideration so we are OK
    entry_list_p = \
      static_cast<HashEntry *>(malloc(sizeof(HashEntry) * entry_count));
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
    free(entry_list_p);
    
    return;
  }
};

}
}
