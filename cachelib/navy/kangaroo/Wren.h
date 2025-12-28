#pragma once
#include <chrono>
#include <mutex>
#include <shared_mutex>

#include "cachelib/navy/common/Device.h"
#include "cachelib/navy/kangaroo/Types.h"
#include <iomanip>


namespace facebook {
namespace cachelib {
namespace navy {
class Wren  {
 public:
  class EuIterator {
    // Iterator, does not hold any locks
    // need to guarantee no writes to EU during GC process
    public: 
      bool done() { return done_; }
      KangarooBucketId getBucket() { return kbid_; }
			EuIterator() : kbid_{0}, done_{true} {}

    private:
      friend Wren;

      explicit EuIterator(KangarooBucketId kbid): 
            kbid_{kbid} {}
      bool done_ = false;
      KangarooBucketId kbid_;
      int i_ = 0;
  };

  // Throw std::invalid_argument on bad config
  explicit Wren(Device& device, uint64_t numBuckets, 
      uint64_t bucketSize, uint64_t totalSize, uint64_t setOffset);
  ~Wren();

  Wren(const Wren&) = delete;
  Wren& operator=(const Wren&) = delete;

  Buffer read(KangarooBucketId kbid, bool& newBuffer);
  bool write(KangarooBucketId kbid, Buffer buffer);

  bool shouldClean(double cleaningThreshold);
  EuIterator getEuIterator();
  EuIterator getNext(EuIterator); // of next zone to erase
  bool erase(); // will throw error if not all buckets are rewritten
  uint64_t getPybid(KangarooBucketId kbid);
  uint64_t getPyzone(KangarooBucketId kbid);
  void invalidMappingTable(KangarooBucketId kbid);
  void addValid(uint64_t newPybid, uint64_t newPyzone, bool logFlush);
  void subValid(uint64_t oldPybid, uint64_t oldPyzone, bool logFlush);
  double setGCinvalidSetRatio();
  uint64_t getSetGCinvalidSets();
  uint64_t getLogGCinvalidSets();

  mutable AtomicCounter readSSDCount_;

 private:
  Device& device_;

  // Erase Unit Id
  class EuId {
   public:
    explicit EuId(uint64_t idx) : idx_{idx} {}

    bool operator==(const EuId& rhs) const noexcept {
      return idx_ == rhs.idx_;
    }
    bool operator!=(const EuId& rhs) const noexcept {
      return !(*this == rhs);
    }

    uint64_t index() const noexcept { return idx_; }

   private:
    uint64_t idx_;
  };

  struct EuIdentifier {
    EuId euid_; // location on device

    EuIdentifier() : euid_{EuId((uint64_t) -1)} {}
  };
  EuId calcEuId(uint32_t erase_unit, uint32_t offset);
  EuId findEuId(KangarooBucketId kbid);
  uint64_t getEuIdLoc(EuId euid);
  uint64_t getEuIdLoc(uint32_t erase_unit, uint32_t offset);

  // page to zone and offset mapping
  EuIdentifier* kbidToEuid_;

  // only implementing FIFO eviction
  folly::SharedMutex writeMutex_;
  uint64_t euCap_; // actual erase unit usable capacity
  uint64_t numEus_;
  uint64_t writeEraseUnit_{0};
  uint64_t eraseEraseUnit_{0};
  uint32_t writeOffset_{0};
  uint64_t cleaningThreshold_{2};
  uint64_t numBuckets_;
  uint64_t bucketSize_;
  uint64_t setOffset_;
  uint64_t bucketsPerEu_;

  // valid set
  mutable std::vector<AtomicCounter> setStat;
  mutable std::vector<AtomicCounter> validSetsPerzone;
  mutable std::vector<AtomicCounter> logToSetValidSetsPerzone;
  mutable std::vector<AtomicCounter> setGCValidSetsPerzone;
  mutable AtomicCounter setGCInValidSets;
  mutable AtomicCounter logGCInValidSets;


};
} // namespace navy
} // namespace cachelib
} // namespace facebook
