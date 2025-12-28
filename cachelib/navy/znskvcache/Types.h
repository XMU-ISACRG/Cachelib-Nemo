#pragma once 

#include <functional>

#include <cachelib/navy/common/Hash.h>

namespace facebook {
namespace cachelib {
namespace navy {

class ZNSKVCacheBucketId {
 public:
  explicit ZNSKVCacheBucketId(uint32_t idx) : idx_{idx} {}

  bool operator==(const ZNSKVCacheBucketId& rhs) const noexcept {
    return idx_ == rhs.idx_;
  }
  bool operator!=(const ZNSKVCacheBucketId& rhs) const noexcept {
    return !(*this == rhs);
  }

  uint32_t index() const noexcept { return idx_; }

 private:
  uint32_t idx_;
};

using OurSetNumberCallback = std::function<ZNSKVCacheBucketId(uint64_t)>;

}
}
}