
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
 *      reasonably low level, more memory are allocated for efficiency
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
  
  // Size of a VM page used to estimate initial number of entries in the
  // hash table
  static constexpr uint64_t PAGE_SIZE = 4096;
  
  // Number of slots in a KeyValueList when first allocated
  static constexpr uint32_t KVL_INIT_VALUE_COUNT = 4;
  
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
    Data<ValueType> data[0];
    
    /*
     * IsFull() - Return whether the key value list is full and needs
     *            a resize
     */
    bool IsFull() const {
      return size == capacity;
    }
    
    /*
     * GetLastElement() - Return a pointer to the last element in this list
     */
    Data<ValueType> *GetLastElement() {
      return data + size;
    }
    
    /*
     * FillValue() - Fill value at a given index
     *
     * Since the constructor for values in this class is not called, we
     * need to use placement new to call the copy constructor
     */
    void FillValue(uint32_t index, const ValueType &value) {
      assert(index < size);
      
      (data + index)->Init(value);
      
      return;
    }
    
    /*
     * DestroyAllValues() - Calls destructor for all valid value entries in
     *                      this object
     *
     * Since we do not invoke destructor by operator delete, destructor must be
     * called explicitly when this is destroyed
     *
     * Note that this function does not free the memory for itself, so the
     * caller to this function needs to call free() to avoid memory leak
     */
    void DestroyAllValues() {
      assert(size <= capacity);
      
      // Loop on all valid entries and then call destructor
      for(uint32_t i = 0;i < size;i++) {
        (data + i)->Fini();
      }
      
      return;
    }
    
    /*
     * DeleteIndex() - Removes the element on the specified index, and
     *                 elements after it by 1
     *
     * This function calls destructor first, and then calls copy
     * constructor for each slot being shifted into
     *
     * If there are k elements after the deleted item, then destructor
     * will be called (k + 1) times, and copy constructor will be called
     * k times. Therefore this operation is non-trivial
     */
    void DeleteIndex(uint32_t index) {
      assert(index < size);
      
      // Finish it first
      data[index].Fini();
      
      // "from" is the index being copied from
      for(uint32_t from = index + 1;from < size;from++) {
        uint32_t to = from - 1;
        
        // Move it one element ahead
        data[to].Init(data[from]);
        // And then destroy the element
        data[from].Fini();
      }
      
      // This should be done after everything has been finished
      size--;
      
      return;
    }

    /*
     * GetResized() - Double the size of the current instance and return
     *
     * This function returns a pointer to the new instance initialized from
     * the current one with its size doubled without freeing the current one
     */
    KeyValueList *GetResized() {
      // This must be called with size == capacity
      assert(size == capacity);
      
      // Malloc a new instance
      KeyValueList *new_kvl_p = \
        static_cast<KeyValueList *>(
          malloc(KeyValueList::GetAllocSize(capacity << 1)));
      assert(new_kvl_p != nullptr);
          
      // Initialize header
      new_kvl_p->size = size;
      new_kvl_p->capacity = capacity << 1;
      
      // Next initialize value entries
      for(uint32_t i = 0;i < size;i++) {
        // Explicitly call its copy constructor
        new_kvl_p->FillValue(i, *(data + i));
      }
      
      dbg_printf("Resize finished\n");
      
      return new_kvl_p;
    }
    
    /*
     * GetAllocSize() - Static function to compute the size of a kv list
     *                  instance with a certain number of items
     */
    static size_t GetAllocSize(uint32_t data_count) {
      return sizeof(KeyValueList) + data_count * sizeof(Data<ValueType>);
    }
    
    /*
     * GetNew() - Return a newly constructed list without any initialization
     */
    static KeyValueList *GetNew() {
      KeyValueList *kvl_p = static_cast<KeyValueList *>(
        malloc(KeyValueList::GetAllocSize(KVL_INIT_VALUE_COUNT)));
        
      kvl_p->capacity = KVL_INIT_VALUE_COUNT;
      
      return kvl_p;
    }
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
      INLINE_VALUE = 2,
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
    // Also these two will cause constructor to fail
    Data<KeyType> key;
    Data<ValueType> value;
    
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
     * so all status smaller than INLINE_VALUE is considered as being
     * eligible for insertion
     */
    inline bool IsProbeEndForInsert() const {
      return status < StatusCode::INLINE_VALUE;
    }
    
    /*
     * IsValidEntry() - Whether the entry has a key and at least one value
     *
     * The trick here is that for all values >= INLINE_VALUE, they indicate
     * valid entries
     */
    inline bool IsValidEntry() const {
      return status >= StatusCode::INLINE_VALUE;
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
     * HasKeyValueList() - returns if the entry has a key value list
     *
     * The trick here is that we consider a normal pointer value
     * as larger than any of the defined status code
     */
    inline bool HasKeyValueList() const {
      return status >= StatusCode::MULTIPLE_VALUES;
    }
    
    /*
     * Fini() - Destroy key AND/OR value object depending on the current
     *          status of the entry
     *
     *            1. FREE status: Ignore
     *            2. DELETED status: Ignore
     *            3. INLINE_VALUE: Destroy key and value
     *            4. OTHER: Destroy key
     */
    inline void Fini() {
      switch(status) {
        case StatusCode::FREE:
        case StatusCode::DELETED:
          break;
        case StatusCode::INLINE_VALUE:
          value.Fini();
          // FALL THROUGH
        default:
          key.Fini();
      }
      
      return;
    }
    
    /*
     * CopyTo() - Copy the current entry into another entry
     *
     * This function obeys a similar rule as destroying the object.
     *
     * Note that if the hash entry is trivially constritable then we just
     * call memcpy to copy it
     */
    inline void CopyTo(HashEntry *other_p) {
      if((std::is_trivially_copy_constructible<KeyType>::value &&
          std::is_trivially_copy_constructible<ValueType>::value) == false) {
        // If this is a pointer then the pointer is copied
        other_p->status = status;
        other_p->hash_value = hash_value;

        switch(status) {
          case StatusCode::FREE:
          case StatusCode::DELETED:
            break;
          case StatusCode::INLINE_VALUE:
            other_p->value.Init(value);
            // FALL THROUGH
          default:
            other_p->key.Init(key);
        }
      } else {
        // If the object is trivially copy constructible then just trivially
        // copy construct it by a byte copy
        std::memcpy(other_p, this, sizeof(HashEntry));
      }

      return;
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
   *                    the first HashEntry's ValueType pointer that this hash 
   *                    value's key could be inserted into
   *
   * This function returns the value type pointer iff:
   *
   *   1. The entry is deleted
   *   2. The entry is free
   *   3. The entry is neither deleted nor free, but has a key that matches
   *      the given key
   *     3.1 The HashEntry does not have a key value list
   *     3.2 The HashEntry has a key value list
   *
   * In the case of either 1 or 2, the hash value and key is filled in
   * automatically by this function since this function is only called for
   * inserting a new key
   *
   * For 3.1 the KVL is allocated and the current inline value
   * is copy constructed onto that list, and the current value is destroyed
   */
  Data<ValueType> *ProbeForInsert(const KeyType &key) {
    // Compute the starting point for probing the hash table
    uint64_t hash_value = key_hash_obj(key);
    uint64_t index = hash_value & index_mask;
    HashEntry *entry_p = entry_list_p + index;

    // Keep probing until there is a entry that is not free
    // Since we always assume the table does not become entirely full,
    // a free slot could always be inserted
    while(entry_p->IsProbeEndForInsert() == false) {
      // If we have found the key, then directly return
      if(key_eq_obj(key, entry_p->key) == true) {
        if(entry_p->HasKeyValueList() == false) {
          KeyValueList *kv_p = KeyValueList::GetNew();
          assert(kv_p != nullptr);

          // Hook the pointer to the HashEntry
          entry_p->kv_p = kv_p;
          
          // Initialize its header
          // Size is 2 since we copy the previous one into it and then
          // another one will be inserted
          kv_p->size = 2;
          
          // Construct in-place
          kv_p->FillValue(0, entry_p->value);
          
          // We know this value object is valid, and now destroy it since
          // it has been copied into the key value list
          entry_p->value.Fini();
          
          // Return the second element for inserting new values
          return kv_p->data + 1;
        } else if(entry_p->kv_p->IsFull()) {
          // If the size equals capacity then the kv list is full
          // and we should extend the value list
          KeyValueList *kv_p = entry_p->kv_p->GetResized();
          
          // Call destructor explicitly for all existing values
          // after we have copy constructed them inside the new array
          entry_p->kv_p->DestroyAllValues();
          
          free(entry_p->kv_p);
          entry_p->kv_p = kv_p;
        }
        
        // Need to get this before increasing size
        Data<ValueType> *ret = entry_p->kv_p->GetLastElement();
        
        // This should be done whether it is resized or not
        entry_p->kv_p->size++;
        
        // This needs to be called no matter whether resize has been
        // called or not
        return ret;
      }
      
      GetNextEntry(&entry_p, &index);
    }

    // After this pointer we know the key and values are not initialized

    // This is important!!!
    active_entry_count++;

    // Change the status first
    entry_p->status = HashEntry::StatusCode::INLINE_VALUE;
    
    // Then fill in hash and key
    // We leave the value to be filled by the caller
    entry_p->hash_value = hash_value;

    entry_p->key.Init(key);

    // It is either a deleted or free entry
    // which could be determined by only 1 instruction
    return &entry_p->value;
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
   * GetHashEntryListStatic() - Allocates a hash entry list given the number of
   *                            HashEntry objects
   *
   * This will first call malloc() to initialize memory and then initialize
   * status code for each entry to FREE
   *
   * Note that this function allocates a chunk of memory of entry_count +£±
   * entries, in a sense that we use the last entry as a sentinel to support
   * iterating through the entire hash table, i.e. the iterator must stop on
   * the sentinel entry (so it is initialized to INLINE_VALUE)
   */
  static HashEntry *GetHashEntryListStatic(uint64_t entry_count) {
    HashEntry *entry_list_p = \
      static_cast<HashEntry *>(malloc(sizeof(HashEntry) * (1 + entry_count)));
      
    for(uint64_t i = 0;i < entry_count;i++) {
      entry_list_p[i].status = HashEntry::StatusCode::FREE;
    }
    
    // This will be the entry pointed to by the end() iterator
    // and also it stops iteration
    // "remaining" will be set to 1 when iterator hits this entry
    // so we know comparison between them yields true
    entry_list_p[entry_count].status = HashEntry::StatusCode::INLINE_VALUE;
    
    return entry_list_p;
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
    resize_threshold = lfc(entry_count);
    
    // Preserve the old entry list and allocate a new one
    HashEntry *old_entry_list_p = entry_list_p;
    
    // This will initialize status code for each entry
    entry_list_p = HashTable_OA_KVL::GetHashEntryListStatic(entry_count);
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
        
        // This calls the copy constructor for KeyType and ValueType
        // explicitly
        entry_p->CopyTo(new_entry_p);
        
        // And then call destructor explicitly to destroy key AND/OR value
        entry_p->Fini();
      }
      
      // We could directly add here since we scan from the beginning
      entry_p++;
    }
    
    // Free old list to avoid memory leak
    free(old_entry_list_p);
    
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
   * FreeAllHashEntries() - Frees the entire HashEntry array's content
   *
   * This function first frees key and value objects stored inside each
   * HashEntry according to its current status, and then destroies its
   * key value list if it has one
   */
  void FreeAllHashEntries() {
    // We use this variable as end of loop condition
    uint64_t remaining = active_entry_count;
    HashEntry *entry_p = entry_list_p;

    // We use this as an optimization, since as long as we have finished
    // iterating through all valid entries
    while(remaining > 0) {
      // The variable counts valid entry
      if(entry_p->IsValidEntry() == true) {
        remaining--;
        
        // Destroy key and value but not the kv list
        entry_p->Fini();
      }

      // And among all valid entries we only destroy those that have
      // a key value list
      // This will in turn destroy all values manually inside the key value
      // list's value array
      if(entry_p->HasKeyValueList() == true) {
        entry_p->kv_p->DestroyAllValues();
        
        // Avoid memory leak
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
   *
   * The constructor takes key comparator, key hash function and load factor
   * calculator as arguments. If not provided then they are defaulted to be
   * the default initialized object from the functor class
   *
   * Also the initial number of entries in the array could be specified by the
   * caller. If not specified then it is either set to the larger one between
   * hardcoded minimum number of entries, or to PAGE_SIZE / sizeof(HashEntry)
   */
  HashTable_OA_KVL(uint64_t init_entry_count = 0,
                   const KeyHashFunc &p_key_hash_obj = KeyHashFunc{},
                   const KeyEqualityChecker &p_key_eq_obj = KeyEqualityChecker{},
                   const LoadFactorCalculator &p_lfc = LoadFactorCalculator{}) :
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
    
    // This does not call any constructor of any kind, and we only
    // initialize on demand
    entry_list_p = GetHashEntryListStatic(entry_count);
    assert(entry_list_p != nullptr);
    
    dbg_printf("Hash table size = %lu\n", entry_count);
    dbg_printf("Resize threshold = %lu\n", resize_threshold);
    dbg_printf("is_trivially_copy_constructible = %d\n",
               (int)(std::is_trivially_copy_constructible<KeyType>::value &&
                     std::is_trivially_copy_constructible<ValueType>::value));
    
    return;
  }
  
  /*
   * Destructor - Frees all memory, including HashEntry array and KeyValueList
   *
   * This functions first traverses all entries to find valid ones, and frees
   * their KeyValueList if there is one
   */
  ~HashTable_OA_KVL() {
    // Free all key, value and key value list
    FreeAllHashEntries();
    
    // Free the array
    assert(entry_list_p);
    free(entry_list_p);
    
    return;
  }
  
  /*
   * Insert() - Inserts a value into the hash table
   *
   * The key and value will be copy-constructed into the table
   *
   * This function might invalidate all iterators on the hash table in case
   * of a resize(). If no resize happens then it does not invalidate any
   * valid pointer including the End() pointer
   */
  void Insert(const KeyType &key, const ValueType &value) {
    if(active_entry_count == resize_threshold) {
      Resize();
      // This must hold true for any load factor
      assert(active_entry_count < resize_threshold);
    }
    
    // This function fills in hash value and key and chahges the
    // status code automatically if the entry was free or deleted
    // and it returns the pointer to the place where new value should
    // be inserted
    Data<ValueType> *value_p = ProbeForInsert(key);
    value_p->Init(value);
    
    return;
  }
  
 private:

  /*
   * DeleteEntry() - Deletes an existing entry entirely from the hash table
   *
   * This function erases a key togetehr with all its values from the hash
   * table and updates metadata in the table header
   */
  void DeleteEntry(HashEntry *entry_p) {
    assert(entry_p->IsValidEntry() == true);
    
    // If there is a key value list then call destructor for all
    // values first
    if(entry_p->HasKeyValueList() == true) {
      entry_p->kv_p->DestroyAllValues();
      
      // Free its memory to avoid leak
      free(entry_p->kv_p);
    }

    // Call the destructor manually for inlined key AND/OR value
    entry_p->Fini();

    // Mark it as deleted - stop point for insertion, but does not
    // terminate probing for value search
    entry_p->status = HashEntry::StatusCode::DELETED;

    // At last decrease the entry counter
    active_entry_count--;

    return;
  }

 public:
   
  /*
   * DeleteKey() - Deletes a key with all its value(s) from the table
   *
   * If the key does not exist in the hash table then return false; Otherwise
   * return true
   *
   * DeleteKey() invalidates iterators on the entry having key
   */
  bool DeleteKey(const KeyType &key) {
    HashEntry *entry_p = ProbeForSearch(key);
    if(entry_p == nullptr) {
      return false;
    }
    
    // This also updates active_entry_count
    DeleteEntry(entry_p);
    
    return true;
  }
  
  /*
   * GetValue() - Return a pointer to value and number of values
   *
   * The first pointer is returned as a pointer into the ValueType array
   * and the second pointer is returned as the number of elements to
   * fetch as values
   */
  std::pair<ValueType *, uint32_t> GetValue(const KeyType &key) {
    HashEntry *entry_p = ProbeForSearch(key);
    
    // There could be three results:
    //   1. Key not found, return nullptr and value count = 0
    //   2. Key found with > 1 values, return pointer to starting of
    //      the value type array
    //   3. There is only 1 value, retuen the inlined value object
    if(entry_p == nullptr) {
      return std::make_pair(nullptr, 0);
    } else if(entry_p->HasKeyValueList() == true) {
      return std::make_pair(&entry_p->kv_p->data[0].data, entry_p->kv_p->size);
    }
    
    return std::make_pair(&entry_p->value.data, 1);
  }
  
  /*
   * GetFirstValue() - Get the first value stored in the hash table
   *
   * This function only fetches one value from the map even if there are
   * multiple values, to save some overhead if only the first value is cared
   * about
   */
  ValueType *GetFirstValue(const KeyType &key) {
    HashEntry *entry_p = ProbeForSearch(key);

    if(entry_p == nullptr) {
      return nullptr;
    } else if(entry_p->HasKeyValueList() == true) {
      return &entry_p->kv_p->data[0].data;
    }

    return &entry_p->value.data;
  }
  
  /*
   * GetOnlyInlinedValue() - Return inline values for an entry found
   *
   * This function could only be called:
   *
   *   1. The key does not exist in the hash table
   *   2. The key has only one value, and delete() has never been
   *      called on that key (i.e. the value is truly stored in-line)
   */
  ValueType *GetOnlyInlinedValue(const KeyType &key) {
    HashEntry *entry_p = ProbeForSearch(key);

    if(entry_p == nullptr) {
      return nullptr;
    }
    
    // This must hold since the caller assumes there could only be
    // inlined value
    assert(entry_p->HasKeyValueList() == false);

    return &entry_p->value.data;
  }

 private:
  
  /*
   * class Iterator - Supports iterating on the hash table
   *
   * This class is non-standard iterator implementation:
   *
   *   1. The size of the object is larger than the size of a normal iterator
   *      (8 bytes)
   *   2. The ++ and -- operation is not constant time - in the worst case it
   *      could be linear on the size of the hash table
   *      *SO PLEASE* do not use the iterator for full scan unless it is
   *      very necessary
   */
  class Iterator {
    friend class HashTable_OA_KVL;
   private:
    // Current hash entry
    HashEntry *entry_p;
    ValueType *value_p;
    uint32_t remaining;
    
    /*
     * GotoNextEntry() - Moves the cursor to the next valid entry in the
     *                   hash table.
     *
     * Note that in iterator there is generally no bounds checking, and the
     * caller is responsible for making sure it is still valid
     *
     * Note that even if the current entry it points to is a valid entry
     * it still advances to the next
     */
    void GotoNextEntry() {
      entry_p++;
      while(entry_p->IsValidEntry() == false) {
        entry_p++;
      }
      
      return;
    }
    
    /*
     * Advance() - Advance the iterator by 1 element
     *
     * Note that if we advance to a new entry, then the index and size must
     * also change accordingly
     */
    void Advance() {
      // Move index in the current bucket to the next value
      remaining--;
      value_p++;
      
      // If we have reached the end of the current value list
      // which happens everytime for a single map
      if(remaining == 0) {
        GotoNextEntry();
        
        if(entry_p->HasKeyValueList() == false) {
          // Special case: inlined value storage
          // just direct the pointer to the inlined value
          remaining = 1;
          value_p = &entry_p->value.data;
        } else {
          // Then we iterate through the value list
          remaining = entry_p->kv_p->size;
          value_p = &entry_p->kv_p->data[0].data;
        }
      }
      
      return;
    }
    
   public:

    /*
     * Constructor
     */
    Iterator(HashEntry *p_entry_p,
             ValueType *p_value_p,
             uint32_t p_remaining) :
      entry_p{p_entry_p},
      value_p{p_value_p},
      remaining{p_remaining}
    {}
    
    /*
     * Copy Constructor
     */
    Iterator(const Iterator &other) :
      entry_p{other.entry_p},
      value_p{other.value_p},
      remaining{other.remaining}
    {}
    
    /*
     * operator=() - Assignment operator
     */
    Iterator &operator=(const Iterator &other) {
      entry_p = other.entry_p;
      value_p = other.value_p;
      remaining = other.remaining;
      
      return *this;
    }

    /*
    * Prefix operator++() - Advances the iterator by one element
    */
    Iterator &operator++() {
     Advance();

     return *this;
    }

    /*
    * Postfix operator++() - Advances the iterator by one element
    *                        and return the value before ++ operation
    *
    * Note that this operation is slower than the prefix++ since it copy
    * constructs an instance of the value each time it is called
    */
    Iterator operator++(int) {
      // Copy construct one
      // Note that this operation is pretty expensive
      // so use prefix ++ as often as possible
      Iterator ret = *this;

      Advance();

      return ret;
    }

    /*
    * operator==() - Compares two iterators for equality
    *
    * Since "remaining" and value_p actually refers to the same thing, we only
    * compare "remaining"
    */
    bool operator==(const Iterator &other) const {
     return (entry_p == other.entry_p) && \
            (remaining == other.remaining);
    }
    
    /*
     * operator!=() - Compares two iterators for non-equality
     */
    bool operator!=(const Iterator &other) const {
      return !(*this == other);
    }

    /*
     * operator*() - Pointer dereference
     *
     * We return a mutable reference of ValueType to the caller
     * such that the caller is free to modify the value
     * already stored in the hash table
     */
    ValueType &operator*() {
      return *value_p;
    }
    
    /*
     * GetKey() - Returns a constant reference to the key
     *
     * Note that key object stored in the hash table is not allowed to be
     * modified since otherwise the hash entry would be in the wrong position
     */
    const KeyType &GetKey() {
      return entry_p->key.data;
    }
  };
  
 private:

  /*
   * BuildIterator() - Given a HashEntry, build an iterator pointing to
   *                   the first element in that entry
   */
  Iterator BuildIterator(HashEntry *entry_p) {
    ValueType *value_p = &entry_p->value.data;
    uint32_t remaining = 1;

    // If there is a key value list then update value pointer
    // and remaining elements
    if(entry_p->HasKeyValueList() == true) {
      remaining = entry_p->kv_p->size;
      assert(remaining > 0);

      value_p = &entry_p->kv_p->data[0].data;
    }

    // Construct an iterator
    return Iterator{entry_p, value_p, remaining};
  }
  
 public:
  
  /*
   * End() - Returns an iterator that points to the end it of the hash table
   *
   * Note that end iterator is defined as the first entry that is not part of
   * the hash table, so we need a sentinel object to act as a concrete last
   * object which stops iteration when the iterator reaches there.
   */
  inline Iterator End() {
    return BuildIterator(entry_list_p + entry_count);
  }
  
  /*
   * Begin() - Return an iterator pointing to the first element of a given key
   */
  inline Iterator Begin(const KeyType &key) {
    HashEntry *entry_p = ProbeForSearch(key);

    // If element not found just return end() iterator
    if(entry_p == nullptr) {
      return End();
    }

    assert(entry_p->IsValidEntry() == true);

    return BuildIterator(entry_p);
  }

  /*
   * Begin() - Get an iterator pointing to the beinning of the HashTeble
   */
  inline Iterator Begin() {
    // If the hash table is empty then directly return end iterator
    if(active_entry_count == 0) {
      return End();
    }

    HashEntry *entry_p = entry_list_p;
    while(entry_p->IsValidEntry() == false) {
      entry_p++;
    }

    // We are guaranteed that this is not the sentinel
    return BuildIterator(entry_p);
  }

  /*
   * KeyRange() - Return a pair of iterators that point to the starting and
   *              ending point of an element
   */
  inline std::pair<Iterator, Iterator> KeyRange(const KeyType &key) {
    HashEntry *entry_p = ProbeForSearch(key);

    // If element not found just return end() iterator
    if(entry_p == nullptr) {
      return std::make_pair(End(), End());
    }

    assert(entry_p->IsValidEntry() == true);

    Iterator it1 = BuildIterator(entry_p);
    Iterator it2 = it1;

    assert(it2.remaining > 0);

    // Advance its value pointer to point to the last element
    // Note that remaining must > 0 as defined
    it2.value_p += (it2.remaining - 1);
    it2.remaining = 1;

    return {it1, it2};
  }
  
  /*
   * Delete() - Removes one element
   *
   * This function takes an iterator that points to a valid value to be removed
   * since we do not have a value comparator inside the hash table, there is no
   * way for us to compare value directly.
   *
   * Delete() operation invalidates all iterators on the entry being
   * deleted from, but preserves validity of all other iterators
   */
  void Delete(const Iterator &it) {
    HashEntry *entry_p = it.entry_p;

    assert(entry_p->IsValidEntry() == true);

    if(entry_p->HasKeyValueList() == false) {
      // This will update active_entry_count
      DeleteEntry(entry_p);

      return;
    }

    // If not the case then we might be on a key value list

    KeyValueList *kv_p = entry_p->kv_p;

    assert(kv_p->size > 0);
    if(kv_p->size == 1) {
      // If the size of the key value list then it is equivalent to
      // having an inlined value - just destroy the entire entry
      DeleteEntry(entry_p);
    } else {
      // Otherwise just delete the element pointed to by remaining
      // which is an index
      kv_p->DeleteIndex(kv_p->size - it.remaining);
    }

    return;
  }
  
  // Statistical data
 public:
   
  /*
   * GetMaxSearchProbeLength() - Return the maximum run-length of searching
   *
   * This function walks through all hash entries and compute the maximum
   * length of the probing sequence
   */
  uint64_t GetMaxSearchProbeLength() const {
    HashEntry *entry_p = entry_list_p;
    uint64_t count = 1;
    uint64_t max_count = 0;
    
    // Loop through every entry and reset counter for every end point
    // of searching probe
    for(uint64_t i = 0;i < entry_count;i++) {
      if(entry_p->IsProbeEndForSearch() == true) {
        if(count > max_count) {
          max_count = count;
        }
        
        count = 1;
      } else {
        count++;
      }
      
      entry_p++;
    }
    
    return max_count;
  }

  /*
   * GetMeanSearchProbeLength() - Returns the average length of sequences
   */
  double GetMeanSearchProbeLength() const {
    HashEntry *entry_p = entry_list_p;
    uint64_t count = 1;
    
    uint64_t total_count = 0;
    uint64_t sequence_count = 0;
    
    bool previous_valid = false;

    // Loop through every entry and reset counter for every end point
    // of searching probe
    for(uint64_t i = 0;i < entry_count;i++) {
      if(entry_p->IsProbeEndForSearch() == true) {
        // Since we do not count invalid element as sequence of
        // length 1
        if(previous_valid == true) {
          // This marks the end of a sequence, so we add the sequence
          // length into total length and increament seq count
          total_count += count;
          sequence_count++;
        }

        count = 1;
        previous_valid = false;
      } else {
        count++;
        previous_valid = true;
      }

      entry_p++;
    }

    return static_cast<double>(total_count) / \
           static_cast<double>(sequence_count);
  }
};

}
}
