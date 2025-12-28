#pragma once

#include <chrono>
#include <stdexcept>

#include <folly/SharedMutex.h>

#include "cachelib/common/AtomicCounter.h"
#include "cachelib/common/BloomFilter.h"
#include "cachelib/navy/common/Buffer.h"
#include "cachelib/navy/common/Device.h"
#include "cachelib/navy/common/Hash.h"
#include "cachelib/navy/common/SizeDistribution.h"
#include "cachelib/navy/common/Types.h"
#include "cachelib/navy/engine/Engine.h"
#include "cachelib/navy/kangaroo/FwLog.h"
#include "cachelib/navy/kangaroo/LogBucket.h"
#include "cachelib/navy/kangaroo/KangarooSizeDistribution.h"
#include "cachelib/navy/kangaroo/RripBitVector.h"
#include "cachelib/navy/kangaroo/RripBucket.h"
#include "cachelib/navy/kangaroo/Types.h"
#include "cachelib/navy/kangaroo/Wren.h"
#include <iostream>

namespace facebook {
namespace cachelib {
namespace navy {
// Kangaroo is a small item flash-based cache engine. It divides the device into
// a series of buckets. One can think of it as a on-device hash table.
//
// Each item is hashed to a bucket according to its key. There is no size class,
// and each bucket is consisted of various variable-sized items. When full, we
// evict the items in their insertion order. An eviction call back is guaranteed
// to be invoked once per item. We currently do not support removeCB. That is
// coming as part of Navy eventually.
//
// Each read and write via Kangaroo happens in `bucketSize` granularity. This
// means, you will read a full bucket even if your item is only 100 bytes.
// It's also the same for writes. This makes Kangaroo inherently unsuitable for
// large items that will also need large buckets (several KB and above).
//
// However, this design gives us the ability to forgo an in-memory index and
// instead look up our items directly from disk. In practice, this means Kangaroo
// is a flash engine optimized for small items.
class Kangaroo final : public Engine {
 public:
  struct Config {
    uint32_t bucketSize{4 * 1024};
    uint32_t hotBucketSize{0};

    // The range of device that Kangaroo will access is guaranted to be
    // with in [baseOffset, baseOffset + cacheSize)
    bool hotColdSep{true};
    uint64_t cacheBaseOffset{};
    uint64_t totalSetSize{};
    uint64_t hotSetSize{};
    Device* device{nullptr};

    DestructorCallback destructorCb;

    // Optional bloom filter to reduce IO
    std::unique_ptr<BloomFilter> bloomFilter;

    std::unique_ptr<RripBitVector> rripBitVector;
    
    uint64_t mergeThreads{32};

    // Better to underestimate, used for pre-allocating log index
    // only needed for Kangaroo
    uint32_t avgSmallObjectSize{100};
    uint32_t logIndexPartitionsPerPhysical{};

    double flushingThreshold;
    double gcUpperThreshold;
    double gcLowerThreshold;

    double setOverprovisioning{}; // overprovisioning
    uint64_t numBuckets() const { return (1 - setOverprovisioning) * (1 - gcUpperThreshold) * totalSetSize / bucketSize; }
    uint64_t hotBaseOffset() const { 
      uint64_t totalZones = totalSetSize / device->getIOZoneCapSize();
      uint64_t hotZones = totalZones * hotBucketSize / bucketSize;
      return cacheBaseOffset + (totalZones - hotZones) * device->getIOZoneSize(); 
    };

    FwLog::Config logConfig;

    Config& validate();
  };

  // Throw std::invalid_argument on bad config
  explicit Kangaroo(Config&& config);

  ~Kangaroo();

  Kangaroo(const Kangaroo&) = delete;
  Kangaroo& operator=(const Kangaroo&) = delete;

  // Check if the key could exist in bighash. This can be used as a pre-check
  // to optimize cache lookups to avoid calling lookups in an async IO
  // environment.
  //
  // @param hk   key to be checked
  //
  // @return  false if the key definitely does not exist and true if it could.
  bool couldExist(HashedKey hk) override;
  // Look up a key in Kangaroo. On success, it will return Status::Ok and
  // populate "value" with the value found. User should pass in a null
  // Buffer as "value" as any existing storage will be freed. If not found,
  // it will return Status::NotFound. And of course, on error, it returns
  // DeviceError.
  Status lookup(HashedKey hk, Buffer& value) override;

  // Inserts key and value into Kangaroo. This will replace an existing
  // key if found. If it failed to write, it will return DeviceError.
  Status insert(HashedKey hk,
                BufferView value) override;

  // Removes an entry from Kangaroo if found. Ok on success, NotFound on miss,
  // and DeviceError on error.
  Status remove(HashedKey hk) override;

  void flush() override;

  void reset() override;

  void persist(RecordWriter& rw) override;
  bool recover(RecordReader& rr) override;

  void getCounters(const CounterVisitor& visitor) const override;
  
  // return the maximum allowed item size
  uint64_t getMaxItemSize() const override;

  uint64_t bfRejectCount() const { return bfRejectCount_.get(); }

 private:
  struct ValidConfigTag {};
  Kangaroo(Config&& config, ValidConfigTag);

  Buffer readBucket(KangarooBucketId bid, bool hot);
  bool writeBucket(KangarooBucketId bid, Buffer buffer, bool hot);


  // The corresponding r/w bucket lock must be held during the entire
  // duration of the read and write operations. For example, during write,
  // if write lock is dropped after a bucket is read from device, user
  // must re-acquire the write lock and re-read the bucket from device
  // again to ensure they have the newest content. Otherwise, one thread
  // could overwrite another's writes.
  //
  // In short, just hold the lock during the entire operation!
  // folly::SharedMutex& getMutex(KangarooBucketId bid) const {
  //   return mutex_[bid.index() & (kNumMutexes - 1)];
  // }
  folly::SharedMutex& getMutex(KangarooBucketId bid) const {
    return mutex_[bid.index() & (kNumMutexes - 1)];
  }

  KangarooBucketId getKangarooBucketId(HashedKey hk) const {
    return KangarooBucketId{static_cast<uint32_t>(hk.keyHash() % numBuckets_)};
  }

  KangarooBucketId getKangarooBucketIdFromHash(uint64_t hash) const {
    return KangarooBucketId{static_cast<uint32_t>(hash % numBuckets_)};
  }

  uint64_t getBucketOffset(KangarooBucketId bid) const {
    return cacheBaseOffset_ + bucketSize_ * bid.index();
  }

  double bfFalsePositivePct() const;
  void bfRebuild(KangarooBucketId bid, const RripBucket* bucket);
  void bfBuild(KangarooBucketId bid, const RripBucket* bucket); // doesn't clear bf
  bool bfReject(KangarooBucketId bid, uint64_t keyHash) const;

  bool bvGetHit(KangarooBucketId bid, uint32_t keyIdx) const;
  void bvSetHit(KangarooBucketId bid, uint32_t keyIdx) const;

  void moveBucket(KangarooBucketId bid, bool logFlush, int gcMode); // gcMode 0 = log flush, 1 = cold, 2 = hot
  void redivideBucket(RripBucket* hotBucket, RripBucket* coldBucket);

  // void addValidBucket(KangarooBucketId kbid, bool logFlush, bool hot);
  // void subValidBucket(KangarooBucketId kbid, bool logFlush, bool hot);
  void addValidBucket(uint64_t newPybid, uint64_t newPyzone, bool logFlush, bool hot);
  void subValidBucket(uint64_t oldPybid, uint64_t oldPyzone, bool logFlush, bool hot);
  uint64_t getPybid(KangarooBucketId kbid, bool hot);
  uint64_t getPyzone(KangarooBucketId kbid, bool hot);
  

  // Use birthday paradox to estimate number of mutexes given number of parallel
  // queries and desired probability of lock collision.
  static constexpr size_t kNumMutexes = 16 * 1024;
  // const uint64_t kNumMutexes{};

  // Serialization format version. Never 0. Versions < 10 reserved for testing.
  static constexpr uint32_t kFormatVersion = 10;
  
  // Open addressing index overhead
  static constexpr double LogIndexOverhead = 2;


  double setOverprovisioning_;
  // Log flushing and gc code, performed on a separate set of threads

  double flushingThreshold_;
  double gcUpperThreshold_;
  double gcLowerThreshold_; 

  bool shouldLogFlush(); // not parallel
  bool shouldUpperGC(bool hot); // not parallel
  bool shouldLowerGC(bool hot); // not parallel
  void performGC();
  void performLogFlush();
  void gcSetupTeardown(bool hot);
  void cleanSegmentsLoop();
  void cleanSegmentsWaitLoop();
  std::vector<std::thread> cleaningThreads_;
  uint64_t numCleaningThreads_;

  std::mutex cleaningSync_;
  uint64_t cleaningSyncThreads_ = 0;
  std::condition_variable cleaningSyncCond_;
  bool performingLogFlush_ = false;
  int performingGC_ = 0; // 0 = fine, 1 = cold sets, 2 = hot sets
  Wren::EuIterator euIterator_;
  bool killThread_ = false;
  bool enableHot_ = true;
  float hotRebuildFreq_ = 5;

  // calculate moving buckets in a set GC zone
  int64_t movingBucketInEU = 0;
  int64_t movingBucketInCleningSeg = 0;
  
  const DestructorCallback destructorCb_{};
  const uint64_t bucketSize_{};
  const uint64_t hotBucketSize_{};
  const uint64_t cacheBaseOffset_{};
  const uint64_t hotCacheBaseOffset_{};
  const uint64_t numBuckets_{};
  std::unique_ptr<BloomFilter> bloomFilter_;
  std::unique_ptr<RripBitVector> bitVector_;
  std::unique_ptr<FwLog> fwLog_{nullptr};
  bool fwOptimizations_ = true;
  std::chrono::nanoseconds generationTime_{};
  Device& device_;
  std::unique_ptr<Wren> wrenDevice_{nullptr};
  std::unique_ptr<Wren> wrenHotDevice_{nullptr};
  std::unique_ptr<folly::SharedMutex[]> mutex_{
      new folly::SharedMutex[kNumMutexes]};
  // std::unique_ptr<folly::SharedMutex[]> mutex_{};

  double avgFillRatio{0};

  uint64_t totalLogSize{0};
  uint64_t totalSetSize{0};
  bool ifMoveBucketBegin{false};

  mutable std::vector<std::unique_ptr<AtomicCounter>> statFillRatio_;
  mutable std::vector<std::unique_ptr<AtomicCounter>> statNumBuckets_;

  mutable AtomicCounter logToSetPysicalWrite_;
  mutable AtomicCounter coldSetGCPysicalWrite_;
  mutable AtomicCounter hotSetGCPysicalWrite_;
  mutable AtomicCounter logToSetLogicalWrite_;
  mutable AtomicCounter coldSetGCLogicalWrite_;
  mutable AtomicCounter hotSetGCLogicalWrite_;
  mutable AtomicCounter validSpace_;
  mutable AtomicCounter evictedCapcityCount_;
  mutable AtomicCounter evictedBucketCount_;
  mutable AtomicCounter itemCount_;
  mutable AtomicCounter logItemCount_;
  mutable AtomicCounter setItemCount_;
  mutable AtomicCounter insertCount_;
  mutable AtomicCounter logInsertCount_;
  mutable AtomicCounter setInsertCount_;
  mutable AtomicCounter readmitInsertCount_;
  mutable AtomicCounter succInsertCount_;
  mutable AtomicCounter lookupCount_;
  mutable AtomicCounter logLookupCount_;
  mutable AtomicCounter setLookupCount_;
  mutable AtomicCounter logSuccLookupCount_;
  mutable AtomicCounter setSuccLookupCount_;
  mutable AtomicCounter succLookupCount_;
  mutable AtomicCounter setHits_;
  mutable AtomicCounter hotSetHits_;
  mutable AtomicCounter logHits_;
  mutable AtomicCounter removeCount_;
  mutable AtomicCounter succRemoveCount_;
  mutable AtomicCounter evictionCount_;
  mutable AtomicCounter logicalWrittenCount_;
  mutable AtomicCounter physicalWrittenCount_;
  mutable AtomicCounter ioErrorCount_;
  mutable AtomicCounter bfFalsePositiveCount_;
  mutable AtomicCounter bfProbeCount_;
  mutable AtomicCounter bfRejectCount_;
  mutable AtomicCounter checksumErrorCount_;
  mutable AtomicCounter thresholdNotHit_;
  mutable AtomicCounter multiInsertCalls_;
  mutable SizeDistribution sizeDist_;
  mutable KangarooSizeDistribution thresholdSizeDist_;
  mutable KangarooSizeDistribution thresholdNumDist_;
  mutable AtomicCounter reachThreshDroppedCount_;


  static_assert((kNumMutexes & (kNumMutexes - 1)) == 0,
                "number of mutexes must be power of two");
};
} // namespace navy
} // namespace cachelib
} // namespace facebook
