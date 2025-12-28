#pragma once

#include "cachelib/common/BloomFilter.h"
#include "cachelib/navy/znskvcache/RegionBucket.h"
#include "cachelib/navy/znskvcache/ZonePool.h"
#include "cachelib/navy/common/Device.h"
#include "cachelib/navy/engine/Engine.h"
#include "cachelib/navy/Factory.h"
#include "cachelib/navy/znskvcache/Types.h"
#include "cachelib/navy/znskvcache/ThreadPool.cpp"
#include "cachelib/navy/znskvcache/Object.h"
#include "cachelib/navy/znskvcache/PBFGList.h"
#include <fstream>
#include <queue>
#include <vector>

namespace facebook {
namespace cachelib {
namespace navy {

#define DATAMODE 1
#define METADATAMODE 0
#define METADATARATIO 50
#define SECONDARY_DATAREGION	((curDataRegionId + 1) % 2)
#define FLUSH_THRESHOLD 90
#define FLUSH_PROBABILITY 0.1
#define NUM_BUCKETS 275712

class ZNSKVCache final : public Engine {
public:
    struct Config {
        int64_t cacheSize{};
        int64_t cacheBaseOffset{0}; // base Offset of ZNSCache
        uint64_t bucketSize{4 * 1024};
        int8_t bucketRatio{}; // PBFGList Ratio
        Device* device{nullptr};
        int32_t dataZoneOffsetId{0};

        // Bloomfilter params
        size_t elementCount;
        double fpProb;

        // 3.evict
        int32_t startStatZone; // evict start zone
        int32_t step;          // evict step
        bool writeBackMode{0};

        // if enable double Hash
        bool isEnableDoubleHash;
        // if enable double dataRegionBuffer
        bool isEnableDoubleBuffer;
        // if kick objects when there is no enough space in dataRegion bucket
        bool isEnableKickOff;
        // max number of kicking objects (Cuckoo Hash)
        int maxNumKicks;

        Config& validate();
    };
    
    // Throw std::invalid_argument on bad config
    explicit ZNSKVCache(Config&& config);

    ~ZNSKVCache();

    ZNSKVCache(const ZNSKVCache&) = delete;
    ZNSKVCache& operator=(const ZNSKVCache&) = delete;

    bool couldExist(HashedKey hk) override;

    Status lookup(HashedKey hk, Buffer& value) override;
    
    Status insert(HashedKey hk, BufferView value) override;

    // TODO
    Status remove(HashedKey hk) override;

    void flush() override;

    void reset() override;

    void persist(RecordWriter& rw) override;
    bool recover(RecordReader& rr) override;

    void getCounters(const CounterVisitor& visitor) const override;
    
    // return the maximum allowed item size
    uint64_t getMaxItemSize() const override;

    uint64_t bfRejectCount() const { return bfRejectCount_.get(); }

    void flush(std::unique_ptr<ZonePool>& pool, bool isDataRegion);

    // flush data/metaData Region
    void flushRegion(bool isFlushMeta);

    void performFlush(int64_t loc, bool isDataRegion);

    void evictZone(std::unique_ptr<ZonePool>& pool, bool poolType);

    void metaDataRegionSet(HashedKey hk, int32_t bid, int8_t regionOffset);

    void metaDataRegionSet2(int64_t key, int32_t bid);

    BufferView readDataRegionBufferBucket(int32_t bid, HashedKey hk);
    
    RegionBucket* readDataZoneBucket(int32_t bid, int32_t euId);

    Buffer readDataZoneBucketBuffer(int32_t bid, int32_t euId); 

    void readMetaDataRegionBucket(HashedKey hk);

    std::shared_ptr<PBFG> getPBFGFromDevice(int32_t bid);

    std::shared_ptr<PBFG> getPBFGFromDevice2(int32_t bid);

    int16_t scanBucket(RegionBucket* bucket, HashedKey hk, Buffer& value);

    int32_t getMask();

    int32_t getZNSKVCacheBucketId(HashedKey hk) const {
        return int32_t{static_cast<int32_t>(hk.keyHash() % numBuckets)};
    }

    int32_t getZNSKVCacheBucketId2(HashedKey hk) const {
        return int32_t{static_cast<int32_t>(hk.keyHash2() % numBuckets)};
    }

    int32_t getZNSKVCacheBucketIdFromHash(int64_t hash) const {
        return int32_t{static_cast<int32_t>(hash % numBuckets)};
    }

    void calZoneId(int32_t bid, HashedKey hk, std::shared_ptr<PBFG> pbfg, 
                            std::vector<int32_t>& zoneIdList);


    void coldZone(int32_t zoneId);

    void coldAllStepZones();

    void writeBack(int32_t zoneId);

    int getFreeDataRegionId(uint32_t bid, HashedKey hk, BufferView value);

    bool kickLoop(uint32_t bid, HashedKey hk, BufferView value);

    void flushLoop();

    void bfRebuild(uint32_t bid, const RegionBucket* bucket, uint32_t zoneId);

    Status subLookup(int64_t bid, HashedKey hk, Buffer& value);

    Status lookupInSSD(int64_t bid, HashedKey hk, Buffer& value);

    bool subCouldExist(int64_t bid, HashedKey hk);

    void exportToCSV(const std::string& filename, std::vector<double> data, int size, int zoneId);

    bool ifHkEqual(HashedKey hk, HashedKey other);

    bool reInsert(HashedKey hk, BufferView value, int32_t bid);

private:
    struct ValidConfigTag {};
    ZNSKVCache(Config&& config, ValidConfigTag);
    
 
    int64_t cacheSize{};
    int32_t zoneNum{};
    int32_t numBuckets{};
    int16_t bucketRatio{};
    uint64_t bucketSize{};
    // base Offset of ZNSCache
    const int64_t cacheBaseOffset{};
    Device& device;

    
    // Bloomfilter params
    size_t elementCount;
    double fpProb;

    // 1.insert
    uint64_t numInserts{0};
    std::unique_ptr<std::unique_ptr<RegionBucket*[]>[]> dataRegion;
    std::unique_ptr<std::unique_ptr<BloomFilter[]>[]> metaDataRegion;
    int8_t curDataRegionId{0};
    int8_t curMetaDataRegionId{0};
    Buffer mutableView; // init Region
    uint32_t perRowPBFByteSize;  
    std::unique_ptr<ZonePool> dataPool;
    std::unique_ptr<ZonePool> metaDataPool;
    uint32_t numFlushZones{0};
    uint32_t numEvictZones{0};
    int32_t dataZoneOffsetId{0};   
    int32_t metaDataZoneOffsetId;
    std::vector<uint64_t> numObjectPerBucket[2];
    std::vector<double> spaceDistPerBucket[2];

    std::mutex curRegionIdMtx;
    std::shared_mutex evictMutex;
    std::shared_mutex pbfgMutex;
    std::condition_variable cv;
    bool isDataRegionFull{false};
    std::array<std::shared_mutex, NUM_BUCKETS> buckets;

    // 2.lookup
    std::unique_ptr<PBFGList> pbfgList;
    Buffer bufferTmp;

    // 3.evict
    std::unique_ptr<std::unique_ptr<int64_t[]>[]> visitedMask;
    int32_t startStatZone{2}; // evict start zone
    int32_t step{2};          // evict step
    bool writeBackMode{false};

    // ========double mode=======
    // if enable random probability(without double hash)
    bool isEnableRandomP{false};
    // if enable double Hash
    bool isEnableDoubleHash;
    // if enable double dataRegionBuffer
    bool isEnableDoubleBuffer;
    // if kick objects when there is no enough space in dataRegion bucket
    bool isEnableKickOff;
    // max number of kicking objects (Cuckoo Hash)
    int maxNumKicks;

    bool isEnableWaitBeforeFlush{true};
    int maxWaitTimesBeforeFlush{1000};
    int waitTimesBeforeFlush{0};

    // Erase Unit Id (No.idx bucket in current cache space)
    class EuId {
    public:
        explicit EuId(int64_t idx) : idx_{idx} {}

        bool operator==(const EuId& rhs) const noexcept {
        return idx_ == rhs.idx_;
        }
        bool operator!=(const EuId& rhs) const noexcept {
        return !(*this == rhs);
        }

        int64_t index() const noexcept { return idx_; }

    private:
        int64_t idx_;
    };

    EuId calcEuId(int32_t erase_unit, int32_t bid);
    EuId findEuId(int32_t kbid);
    int64_t getEuIdLoc(int32_t erase_unit, int32_t bid);
    int64_t getEuIdLoc(EuId euid);

    int64_t bucketsPerEu;
    int64_t euCap;
    int64_t euSize;

    uint64_t realLogicalWrite{0};
    uint64_t reInsertWrite{0};

    mutable std::vector<std::shared_ptr<AtomicCounter>> statHitBid_;
    mutable std::vector<std::shared_ptr<AtomicCounter>> statHitNvmBid_;

    mutable AtomicCounter PBFGListHitMiss_;
    mutable AtomicCounter readSSDCount_;
    mutable AtomicCounter reInsertCount_;
    mutable AtomicCounter itemCount_;
    mutable AtomicCounter readCount_;
    mutable AtomicCounter lookupInSSDCount_;
    mutable AtomicCounter removeCount_;
    mutable AtomicCounter succRemoveCount_;
    mutable AtomicCounter evictionCount_;
    mutable AtomicCounter logicalWrittenCount_;
    mutable AtomicCounter physicalWrittenCount_;
    mutable AtomicCounter ioErrorCount_;
    mutable AtomicCounter bfFalsePositiveCount_;
    mutable AtomicCounter bfProbeCount_;
    mutable AtomicCounter bfRejectCount_;
    mutable AtomicCounter insertCount_;
    mutable AtomicCounter succInsertCount_;
    mutable AtomicCounter readmitInsertCount_; 
    mutable AtomicCounter lookupCount_;
    mutable AtomicCounter succLookupCount_;
    mutable AtomicCounter dataRegionHits_;
    mutable AtomicCounter metaDataRegionHits_;
};


} // namespace navy
} // namespace cachelib
} // namespace facebook