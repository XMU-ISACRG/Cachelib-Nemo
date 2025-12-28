#pragma once 

#include <functional>

#include <cachelib/navy/common/Hash.h>

namespace facebook {
namespace cachelib {
namespace navy {

using BitVectorUpdateVisitor = std::function<void(uint32_t)>;
using BitVectorReadVisitor = std::function<bool(uint32_t)>;

// Kangaroo Log Structures
class LogSegmentId {
 public:
  LogSegmentId(uint32_t offset, uint32_t zone) : 
    offset_{offset}, zone_{zone} {}
  LogSegmentId() {}
  LogSegmentId(LogSegmentId& rhs) : 
    offset_{rhs.offset_}, zone_{rhs.zone_} {}

  bool operator==(const LogSegmentId& rhs) const noexcept {
    return offset_ == rhs.offset_ && zone_ == rhs.zone_;
  }
  bool operator!=(const LogSegmentId& rhs) const noexcept {
    return !(*this == rhs);
  }

  uint32_t offset() const noexcept { return offset_; }
  uint32_t zone() const noexcept { return zone_; }

 private:
  uint32_t offset_; // offset in zone
  uint32_t zone_; // represents the zone
};

class LogPageId {
 public:
  explicit LogPageId(uint32_t idx, bool valid) : idx_{idx}, valid_{valid} {}
  LogPageId() : idx_{0}, valid_{false} {}

  bool operator==(const LogPageId& rhs) const noexcept {
    if (!valid_ && !rhs.valid_) {
      return true;
    }
    return valid_ == rhs.valid_ && idx_ == rhs.idx_;
  }
  bool operator!=(const LogPageId& rhs) const noexcept {
    return !(*this == rhs);
  }

  uint32_t index() const noexcept { return idx_; }
  bool isValid() const noexcept { return valid_; }

 private:
  uint32_t idx_;
  bool valid_;
};

class PartitionOffset {
 public:
  explicit PartitionOffset(uint32_t idx, bool valid) : idx_{idx}, valid_{valid} {}
  PartitionOffset() : idx_{0}, valid_{false} {}

  bool operator==(const PartitionOffset& rhs) const noexcept {
    if (!valid_ && !rhs.valid_) {
      return true;
    }
    return valid_ == rhs.valid_ && idx_ == rhs.idx_;
  }
  bool operator!=(const PartitionOffset& rhs) const noexcept {
    return !(*this == rhs);
  }

  uint32_t index() const noexcept { return idx_; }
  bool isValid() const noexcept { return valid_; }

 private:
  uint32_t idx_;
  bool valid_;
};

class KangarooBucketId {
 public:
  explicit KangarooBucketId(uint32_t idx) : idx_{idx} {}

  bool operator==(const KangarooBucketId& rhs) const noexcept {
    return idx_ == rhs.idx_;
  }
  bool operator!=(const KangarooBucketId& rhs) const noexcept {
    return !(*this == rhs);
  }

  uint32_t index() const noexcept { return idx_; }

 private:
  uint32_t idx_;
};

using SetNumberCallback = std::function<KangarooBucketId(uint64_t)>;

struct ObjectInfo {
  HashedKey key;
  Buffer value;
  uint8_t hits;
  LogPageId lpid;
  uint32_t tag;

  ObjectInfo(HashedKey k, BufferView v, uint8_t h, LogPageId l, uint32_t t):
    key{std::move(k)},
    value{v}, 
    hits{h}, 
    lpid{l}, 
    tag{t} {}
};
using ReadmitCallback = std::function<void(std::unique_ptr<ObjectInfo>&)>;
using SetMultiInsertCallback = std::function<void(std::vector<std::unique_ptr<ObjectInfo>>&, ReadmitCallback)>;
// @key and @value are valid only during this callback invocation
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
