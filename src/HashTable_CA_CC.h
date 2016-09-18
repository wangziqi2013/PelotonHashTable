
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

template <typename KeyType,
          typename ValueType,
          typename KeyHashFunc = std::hash<KeyType>,
          typename KeyEqualityChecker = std::equal_to<KeyType>>
class HashTable_CA_CC {
  
};

}
}
