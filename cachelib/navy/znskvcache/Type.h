#pragma once 

#include <functional>

#include <cachelib/navy/common/Hash.h>

namespace facebook {
namespace cachelib {
namespace navy {

using BitVectorUpdateVisitor = std::function<void(uint32_t)>;
using BitVectorReadVisitor = std::function<bool(uint32_t)>;

class ZNSCacheBucketId {
 public:
  explicit ZNSCacheBucketId(uint32_t idx) : idx_{idx} {}

  bool operator==(const ZNSCacheBucketId& rhs) const noexcept {
    return idx_ == rhs.idx_;
  }
  bool operator!=(const ZNSCacheBucketId& rhs) const noexcept {
    return !(*this == rhs);
  }

  uint32_t index() const noexcept { return idx_; }

 private:
  uint32_t idx_;
};

using SetNumberCallback = std::function<ZNSCacheBucketId(uint64_t)>;
using RedivideCallback =
    std::function<void(HashedKey hk, BufferView value, uint8_t rrip)>;
  
static const uint32_t maxTagValue = 1 << 9;
static const int tagSeed = 23;
static uint32_t createTag(HashedKey hk) {
  return hashBuffer(makeView(hk.key()), tagSeed) % maxTagValue;
} 

} // namespace navy
} // namespace cachelib
} // namespace facebook