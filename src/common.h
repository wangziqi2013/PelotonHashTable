
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
  inline uint64_t operator()(uint64_t table_size) const {
    return table_size >> 1;
  }
};

/*
 * class LoadFactorHalfFull - Compute load factor as 0.75
 */
class LoadFactorThreeFourthFull {
 public:

  /*
   * operator() - Computes the resize threshold given the current table size
   *
   * This will be only called during initialization and table resizing
   */
  inline uint64_t operator()(uint64_t table_size) const {
    return (table_size >> 1) | (table_size >> 2);
  }
};

/*
 * class LoadFactorPercent - Compute load factor as a percentage value
 *
 * This class is templatized and encodes the load factor as template
 * argument
 *
 * In the case that division is not exact, we always round it down to avoid
 * too high load factor leading to an entirely full table
 */
template<uint64_t percentage>
class LoadFactorPercent {
 public:

  /*
   * operator() - Computes the resize threshold given the current table size
   *
   * This will be only called during initialization and table resizing
   */
  inline uint64_t operator()(uint64_t table_size) const {
    return table_size * percentage / 100;
  }
};

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
   
  /*
   * operator()() - Mimics function call
   *
   * Note that this function must be denoted as const since in STL all
   * hashers are stored as a constant object
   */
  inline uint64_t operator()(uint64_t value) const {
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

/*
 * class ConstantZero - Returns zero for all hash requests
 */
class ConstantZero {
 public:
  inline uint64_t operator()(uint64_t value) const {
    return 0;
  }
};

/*
 * class FixedLenValue - This is an object that represents a fixed length
 *                       object which is used for testing
 */
template <size_t sz>
class FixedLenValue {
 public:
  char data[sz];
};

/*
 * class Data - Explicitlly managed data wrapping class
 *
 * This class could not be implicitly constructed or destroyed, and we
 * could only malloc() a chunk of memory and manually call the corresponding
 * routine on its pointer.
 */
template <typename T>
class Data {
 public:
  // Wrapped data
  T data;

  Data() = delete;
  Data(const Data &) = delete;
  Data(Data &&) = delete;
  Data &operator=(const Data &other) = delete;
  Data &operator=(Data &&other) = delete;
  ~Data() = delete;

  /*
   * operator T - Type cast to its wrapped type
   */
  inline operator T&() {
    return data;
  }

  /*
   * Init(const T &) - Copy-construct
   */
  inline void Init(const T &value) {
    new (this) T{value};
  }

  /*
   * Init() - Explcit default construct the object
   */
  inline void Init() {
    new (this) T{};
  }

  /*
   * Fini() - Explicitly destroy the data object
   */
  inline void Fini() {
    data.~T();
  }
};
