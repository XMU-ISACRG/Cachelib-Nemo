#include "cachelib/navy/znskvcache/ZNSKVCache.h"
#include "cachelib/navy/common/Buffer.h"
#include <folly/Random.h>
#include <iostream>
#include <chrono>
#include <mutex>
#include <shared_mutex>

namespace facebook {
namespace cachelib {
namespace navy {

ZNSKVCache::Config& ZNSKVCache::Config::validate() {
    if (cacheSize < bucketSize) {
        throw std::invalid_argument(
            folly::sformat("cache size: {} cannot be smaller than bucket size: {}",
                        cacheSize,
                        bucketSize));
    }

    if (!folly::isPowTwo(bucketSize)) {
        throw std::invalid_argument(
            folly::sformat("invalid bucket size: {}", bucketSize));
    }

    if (cacheBaseOffset % bucketSize != 0 || cacheSize % bucketSize != 0) {
        throw std::invalid_argument(folly::sformat(
            "cacheBaseOffset and totalCacheSize need to be a multiple of bucketSize. "
            "cacheBaseOffset: {}, cacheSize:{}, bucketSize: {}.",
            cacheBaseOffset,
            cacheSize,
            bucketSize));
    }

    // if (device == nullptr) {
    //     throw std::invalid_argument("device cannot be null");
    // }

    return *this;
}

ZNSKVCache::ZNSKVCache(Config&& config)
    : ZNSKVCache{std::move(config.validate()), ValidConfigTag{}} {}

ZNSKVCache::ZNSKVCache(Config&& config, ValidConfigTag)
    : cacheSize{config.cacheSize}, 
      cacheBaseOffset{config.cacheBaseOffset},
      bucketSize{config.bucketSize},
      device{*config.device},
      dataZoneOffsetId{config.dataZoneOffsetId},
      bucketRatio{config.bucketRatio},
      elementCount{config.elementCount},
      fpProb{config.fpProb},
      startStatZone{config.startStatZone},
      step{config.step},
      writeBackMode{config.writeBackMode},
      isEnableDoubleHash{config.isEnableDoubleHash},
      isEnableDoubleBuffer{config.isEnableDoubleBuffer},
      isEnableKickOff{config.isEnableKickOff},
      maxNumKicks{config.maxNumKicks} {
    XLOG(INFO) << "startStatZone = " << startStatZone << ", step = " << step;
    // 1.create ZNSDevice
    euSize = device.getIOZoneSize();
    euCap = device.getIOZoneCapSize();
    numBuckets = euCap / bucketSize;
    bucketsPerEu = numBuckets;
    zoneNum = cacheSize / euCap;
    XLOG(INFO) << folly::sformat("cacheSize = {}, cacheBaseOffset = {}, euCapacity = {}, numBuckets = {}, bucketsPerEu = {}, zoneNum = {}", 
                                    cacheSize, cacheBaseOffset, euCap, numBuckets, bucketsPerEu, zoneNum);
    numObjectPerBucket[0].resize(numBuckets);
    numObjectPerBucket[1].resize(numBuckets);
    XLOG(INFO) << folly::sformat("Double Hash/Region config info: isEnableDoubleHash = {}, isEnableDoubleBuffer = {}, isEnableKickOff = {}, maxNumKicks = {}", 
        isEnableDoubleHash, isEnableDoubleBuffer, isEnableKickOff, maxNumKicks);
    spaceDistPerBucket[0].resize(numBuckets);
    spaceDistPerBucket[1].resize(numBuckets);

    // 2.init data region
    // 2.1 cast pointer
    dataRegion = std::make_unique<std::unique_ptr<RegionBucket*[]>[]>(2);
    dataRegion[0] = std::make_unique<RegionBucket*[]>(numBuckets);
    dataRegion[1] = std::make_unique<RegionBucket*[]>(numBuckets);
    // 2.2 initialize all of the DataRegion Buckets after cast
    mutableView = Buffer(numBuckets * bucketSize * 2);
    XDCHECK(!mutableView.isNull());
    
    for (int64_t i = 0; i < numBuckets; i++) {
        int64_t bucketOffset = i * bucketSize;
        auto view = MutableBufferView(bucketSize, mutableView.data() + bucketOffset);
        auto view1 = MutableBufferView(bucketSize, mutableView.data() + bucketOffset 
                                        + numBuckets * bucketSize);
        RegionBucket::initNew(view, 0);
        RegionBucket::initNew(view1, 0);
        dataRegion[0][i] = reinterpret_cast<RegionBucket*>(mutableView.data() + bucketOffset);
        dataRegion[1][i] = reinterpret_cast<RegionBucket*>(mutableView.data() + bucketOffset 
                                        + numBuckets * bucketSize);
        dataRegion[0][i]->clear();
        dataRegion[1][i]->clear();
    }

    XLOG(INFO) << "dataRegion[0][0] slotSize = " << dataRegion[0][0]->slotSize();
    XLOG(INFO) << "sizeof dataRegion[0][0] = " << dataRegion[0][0]->space();

    for (int i = 0; i < numBuckets + 10; i ++) {
        statHitBid_.push_back(std::make_shared<AtomicCounter>(0));
        statHitNvmBid_.push_back(std::make_shared<AtomicCounter>(0));
    }

    // 3.init metadata region
    metaDataRegion = std::make_unique<std::unique_ptr<BloomFilter[]>[]>(2);
    metaDataRegion[0] = std::make_unique<BloomFilter[]>(numBuckets);
    metaDataRegion[1] = std::make_unique<BloomFilter[]>(numBuckets);
    //calculate numHashes and bitsPerFilter by elementCount and fpProb to construct the BloomFilter
    for (int32_t i = 0; i < numBuckets; ++i) {
        metaDataRegion[0][i] = BloomFilter::makeBloomFilter(METADATARATIO, elementCount, fpProb); 
        metaDataRegion[1][i] = BloomFilter::makeBloomFilter(METADATARATIO, elementCount, fpProb); 
    }
    perRowPBFByteSize = metaDataRegion[0][0].getByteSize();
    std::cout << "perRowPBFByteSize: " << perRowPBFByteSize << std::endl;

    // 4.init dataPool, metaDataPool
    int32_t dataPoolSize = zoneNum / (METADATARATIO + 1) * METADATARATIO; 
    metaDataZoneOffsetId = dataZoneOffsetId + dataPoolSize + 1;
    int32_t metadataPoolSize = zoneNum / (METADATARATIO + 1) * 1;
    dataPool = std::make_unique<ZonePool>(dataZoneOffsetId, dataPoolSize);
    metaDataPool = std::make_unique<ZonePool>(metaDataZoneOffsetId, metadataPoolSize);
    XLOG(INFO) << "DataPoolSize: " << dataPoolSize;
    XLOG(INFO) << "MetaPoolSize: " << metadataPoolSize;

    // 5.lookup
    pbfgList = std::make_unique<PBFGList>(numBuckets * bucketRatio / 100);

    // 6.evict
    int32_t numMaskZones = dataPoolSize + 1;
    visitedMask = std::make_unique<std::unique_ptr<int64_t[]>[]>(numMaskZones);
    for (int32_t i = 0; i < numMaskZones; i++) {
        visitedMask[i] = std::make_unique<int64_t[]>(numBuckets);
    }
    reset();
}

void ZNSKVCache::reset() {
  XLOG(INFO, "Reset ZNSKVCache");


  itemCount_.set(0);
  insertCount_.set(0);
  succInsertCount_.set(0);
  lookupCount_.set(0);
  succLookupCount_.set(0);
  removeCount_.set(0);
  succRemoveCount_.set(0);
  evictionCount_.set(0);
  logicalWrittenCount_.set(0);
  physicalWrittenCount_.set(0);
  ioErrorCount_.set(0);
  bfFalsePositiveCount_.set(0);
  bfProbeCount_.set(0);

}

ZNSKVCache::EuId ZNSKVCache::calcEuId(int32_t erase_unit, int32_t bid) {
    int64_t euOffset = erase_unit * bucketsPerEu;
    return EuId(euOffset + bid);
}

ZNSKVCache::EuId ZNSKVCache::findEuId(int32_t bid) {
    XDCHECK(bid < numBuckets);
}

int64_t ZNSKVCache::getEuIdLoc(int32_t erase_unit, int32_t bid) {
    return getEuIdLoc(calcEuId(erase_unit, bid));
}

// return the offset bytes in device for (No.euid bucket in current Cache Space)
int64_t ZNSKVCache::getEuIdLoc(EuId euid) {
    int64_t zone_offset = euid.index() % bucketsPerEu;    // No.zone_offset bucket in EU
    int64_t zone = euid.index() / bucketsPerEu;           // zone id
    int64_t offset = zone * euSize + zone_offset * bucketSize;  // offset bytes in device
    return offset;
}

void ZNSKVCache::flushRegion(bool isFlushMeta) {
    if (dataPool->isFull()) {
        evictZone(dataPool, DATAMODE);

        // evict metaDataZone according to numEvictedDataZones
        if (++numEvictZones % METADATARATIO == 0) {
            evictZone(metaDataPool, METADATAMODE);
            numEvictZones = 0;
        }
    }
    
    flush(dataPool, DATAMODE);
    
    numInserts = 0;

    // if flush metaDataRegion
    if (isFlushMeta) {
        flush(metaDataPool, METADATAMODE);
    }
}

void ZNSKVCache::performFlush(int64_t loc, bool regionType){
    Buffer ioBuffer(euCap, bucketSize); // ioBuffer.data() should align to ioAlignmentSize(blockSize)

    XDCHECK(!ioBuffer.isNull());
    // 2.slit the data im memory and transfer to numerous small buffers
    // finally integrate small buffers to a large buffer

    // 3.simultaneously clear/reset the data in the region
    int16_t euId = 0;
    if (regionType == DATAMODE) {
        int16_t flushRegionId = SECONDARY_DATAREGION; 
        euId = flushRegionId;
        uint64_t numObj = 0;
        for (int bid = 0; bid < numBuckets; ++bid){
            // std::memcpy(ioBuffer.data() + bid * bucketSize, 
            //     dataRegion[flushRegionId][bid], sizeof(*dataRegion[flushRegionId][bid]));
            std::memcpy(ioBuffer.data() + bid * bucketSize, 
                dataRegion[flushRegionId][bid], bucketSize);
            spaceDistPerBucket[flushRegionId][bid] = dataRegion[flushRegionId][bid]->fillRatio();
            numObj += numObjectPerBucket[flushRegionId][bid];

            // take RegionBucket structure's(entry) size into account
            XDCHECK_LE(numObjectPerBucket[flushRegionId][bid] * 14, dataRegion[flushRegionId][bid]->capacity() - dataRegion[flushRegionId][bid]->remainingCapacity());
            
                logicalWrittenCount_.add(dataRegion[flushRegionId][bid]->capacity() - dataRegion[flushRegionId][bid]->remainingCapacity() - numObjectPerBucket[flushRegionId][bid] * 14);
            
            numObjectPerBucket[flushRegionId][bid] = 0;
            dataRegion[flushRegionId][bid]->clear();
        }
        logicalWrittenCount_.sub(reInsertCount_.get() - reInsertWrite);
        numObj = 0;        

    } else {
        int16_t flushMetaRegionId = (curMetaDataRegionId + 1) % 2; 
        euId = flushMetaRegionId;
        XLOG(INFO) << "flush MetaDataRegion";
        for (int bid = 0; bid < numBuckets; ++bid) {
            std::memcpy(ioBuffer.data() + bid * bucketSize, 
                metaDataRegion[flushMetaRegionId][bid].getBitArray(), 
                metaDataRegion[flushMetaRegionId][bid].getByteSize());
            metaDataRegion[flushMetaRegionId][bid].reset();
        }
        
        XLOG(INFO) << "flush MetaDataRegion done";
    }
    
    // 4.write to the device
    // XLOG(INFO) << "write begin!";

    realLogicalWrite = logicalWrittenCount_.get();
    reInsertWrite = reInsertCount_.get();

    bool ret = device.write(loc, std::move(ioBuffer)); // return 1 if succeed
    
    if (ret) {
        XLOG(INFO) << "successfully flush a region!";
    } else {
        XLOG(INFO) << "failed to flush a region!";
    }
}

void ZNSKVCache::flush(std::unique_ptr<ZonePool>& pool, bool regionType) {
    int32_t flushEuId = pool->getZoneNextId();
    int64_t loc = getEuIdLoc(flushEuId, 0);

    if (regionType == METADATAMODE) {
        int8_t flushMetaRegionId = (curMetaDataRegionId + 1) % 2; 
        
        std::unique_lock<std::shared_mutex> lock(pbfgMutex);

        pbfgList->flushChangeAllPBFG(metaDataRegion[flushMetaRegionId]);
    }

    performFlush(loc, regionType);

    if (regionType == DATAMODE && writeBackMode) {
        // 3.cold zones by visitedMask
        coldAllStepZones();
    }

    pool->enqueue();
}

bool ZNSKVCache::couldExist(HashedKey hk) {
    // get bid
    int64_t bid1 = getZNSKVCacheBucketId(hk);
    int64_t bid2 = getZNSKVCacheBucketId2(hk);
    
    bool result = subCouldExist(bid1, hk);

    // bid2
    if (result == false && isEnableDoubleHash)
        result = subCouldExist(bid2, hk);

    return result;
}

bool ZNSKVCache::subCouldExist(int64_t bid, HashedKey hk) {
    Buffer value;
    // if in memory dataRegion
    {
        std::unique_lock<std::shared_mutex> lock(buckets[bid]);

        int16_t ret = scanBucket(dataRegion[curDataRegionId][bid], hk, value);
        if (ret != -1)    return true;
        else {
            if (isEnableDoubleBuffer) {
                ret = scanBucket(dataRegion[SECONDARY_DATAREGION][bid], hk, value);
                if (ret != -1)    return true;
            }
        }
    }
    // else, find in ZNS ssd (to find zones which may contain KV)
    // get corresponding bucket BFs ==> get potential zones ==> confirm existence by scanBucket
    // (1) get corresponding bucket BFs
    // (1.1) hot
    std::shared_ptr<PBFG> pbfg;
    Status isGetHotPBFG;
    {
        std::shared_lock<std::shared_mutex> lock(pbfgMutex);
        isGetHotPBFG = pbfgList->getPBFG(bid, pbfg);
    }
    
    // (1.2) cold
    if (isGetHotPBFG == Status::NotFound) {
        // XLOG(INFO) << "isGetHotPBFG: NotFound";
        pbfg = getPBFGFromDevice(bid);
    }

    // (2) get potential zones by pbfg
    std::vector<int32_t> zoneIdList(0);
    calZoneId(bid, hk, pbfg, zoneIdList);

    // (3) confirm existence by scanBucket
    bool found = false;
    for (int32_t i = zoneIdList.size() - 1; i >= 0; --i) {
        int32_t zoneId = zoneIdList[i];
        auto buffer = readDataZoneBucketBuffer(bid, zoneId);
        XDCHECK(!buffer.isNull());
        auto* bucket = reinterpret_cast<RegionBucket*>(buffer.data());
        int16_t objId = scanBucket(bucket, hk, value);
        if (objId != -1) {
            found = true;
            break;
        }
    }
    return found;
}

Status ZNSKVCache::insert(HashedKey hk, BufferView value) {
    // 1.get bucket id
    int64_t bid1 = getZNSKVCacheBucketId(hk);
    int64_t bid2 = getZNSKVCacheBucketId2(hk);
    int64_t bid = bid1;

    insertCount_.inc();

    // 2.dataRegion->bucket[bid] insert 
    // (regionId, bucketId): the final place to insert KV
    { 
    std::unique_lock<std::shared_mutex> lock1(buckets[bid1]);
    // std::unique_lock<std::mutex> lock2(buckets[bid2], std::defer_lock);

    bool shouldInsert = true; //whether to execute insert operation in this function
    int8_t regionId = getFreeDataRegionId(bid1, hk, value);
    int8_t regionId2 = getFreeDataRegionId(bid2, hk, value);
    if (regionId != -1) {
        // can insert to bucket1; (region1 or region2)
        if (isEnableDoubleHash && regionId2 != -1) {
            if (dataRegion[regionId][bid1]->remainingCapacity() < dataRegion[regionId2][bid2]->remainingCapacity()) {
                regionId = regionId2;
                bid = bid2;
            }
        }
    // if (regionId != -1) {
    //     // can insert to bucket1; (region1 or region2)
    } else if (isEnableDoubleHash && regionId2 != -1) {
        // can insert to bucket2; (region1 or region2)
        bid = bid2;
        regionId = regionId2;
    } else { // two regions are too "full" to insert => need to flush
        

        // XLOG(INFO) << "waitTimesBeforeFlush  = " << waitTimesBeforeFlush;
        if (++waitTimesBeforeFlush <= maxWaitTimesBeforeFlush && isEnableWaitBeforeFlush) { 
            regionId = isEnableDoubleBuffer ? SECONDARY_DATAREGION : curDataRegionId;
            while(getFreeDataRegionId(bid, hk, value) == -1) {
                dataRegion[regionId][bid]->remove(dataRegion[regionId][bid]->getFirst().hashedKey(), nullptr);
                --numObjectPerBucket[regionId][bid];
                itemCount_.dec();
            }
            bfRebuild(bid, dataRegion[regionId][bid], regionId);
        } else { 
            if (isDataRegionFull) {
                std::unique_lock<std::mutex> lock(curRegionIdMtx); 
                while (isDataRegionFull) {
                    cv.wait(lock);
                }
            } else {
                isDataRegionFull = true;

                if (isEnableWaitBeforeFlush) waitTimesBeforeFlush = 0;
                if (isEnableKickOff) { 
                    if (isEnableDoubleHash) {
                        shouldInsert = false;
                        int64_t randomBid = folly::Random::rand32(0, 2) == 0 ? bid1 : bid2;
                        bool ifFlushDataRegion = kickLoop(randomBid, hk, value);
                        if (ifFlushDataRegion) {
                            {
                                std::unique_lock<std::mutex> lock(curRegionIdMtx);
                                flushLoop();
                                isDataRegionFull = false;
                                cv.notify_all();
                            }
                        }
                    }
                } else {
                    {
                        std::unique_lock<std::mutex> lock(curRegionIdMtx);
                        flushLoop();
                        isDataRegionFull = false;
                        cv.notify_all();
                    }
                }
                regionId = getFreeDataRegionId(bid1, hk, value);
                    
                if (regionId == -1 && isEnableDoubleHash) {
                    regionId = getFreeDataRegionId(bid2, hk, value);
                    bid = bid2;
                }
            }
        }
    }
    if (shouldInsert) {
        {
            std::unique_lock<std::mutex> lock(curRegionIdMtx); 
            while (isDataRegionFull) {
                cv.wait(lock);
            }
        }
        regionId = getFreeDataRegionId(bid, hk, value);
        
        RegionBucketStorage::Allocation alloc = dataRegion[regionId][bid]->allocate(hk, value);
        dataRegion[regionId][bid]->insert(alloc, hk, value);
        numObjectPerBucket[regionId][bid]++;
    
        // 3.metaDataRegion->bucket[bid] insert
        int8_t rigionOffset = (regionId == curDataRegionId) ? 0 : 1;
        metaDataRegionSet(hk, bid, rigionOffset);
        ++numInserts;
        succInsertCount_.inc();
        itemCount_.inc();
    }
    return Status::Ok;

    }

}

void ZNSKVCache::flushLoop() {
    curDataRegionId = (curDataRegionId + 1) % 2;
    bool isFlushMeta = false;
    if ((numFlushZones + 1) % METADATARATIO == 0) {
        curMetaDataRegionId = (curMetaDataRegionId + 1) % 2;
        // numFlushZones = 0;
        isFlushMeta = true;
    }
    // create a new thread for flush region
    flushRegion(isFlushMeta);

    // numFlushZones's change should keep up with dataPool->getNextId
    ++numFlushZones;
    if (isFlushMeta) numFlushZones = 0;
}


int ZNSKVCache::getFreeDataRegionId(uint32_t bid, HashedKey hk, BufferView value) {
    if (dataRegion[curDataRegionId][bid]->isSpace(hk, value))
        return curDataRegionId;

    if (isEnableDoubleBuffer && 
        dataRegion[SECONDARY_DATAREGION][bid]->ifNotReachBucketThreshold(hk, value, FLUSH_THRESHOLD))
            return SECONDARY_DATAREGION;
    return -1;
}

bool ZNSKVCache::kickLoop(uint32_t bid, HashedKey hk, BufferView value) {
    ++numInserts; 
    succInsertCount_.inc();
    uint16_t numKicks = 0;
    bool flushFlag = false; 

    std::queue<std::pair<uint32_t, std::pair<HashedKey, BufferView>>> q; 
    q.push({bid, {hk, value}});
    while(!q.empty()) { 
        std::pair<uint32_t,std::pair<HashedKey, BufferView>> head = q.front(); 
        std::pair<HashedKey, BufferView> obj = head.second;
        int64_t curBid = head.first;
        
        int64_t kickRegionId = getFreeDataRegionId(curBid, obj.first, obj.second);
        if (kickRegionId == -1) {
            kickRegionId = isEnableDoubleBuffer ? SECONDARY_DATAREGION : curDataRegionId;
        }
        
        while(getFreeDataRegionId(curBid, obj.first, obj.second) == -1) { 
            std::pair<HashedKey, BufferView> randomObj(dataRegion[kickRegionId][curBid]->getFirst().hashedKey().key(), 
                                                      dataRegion[kickRegionId][curBid]->getFirst().value());

            
            dataRegion[kickRegionId][curBid]->remove(randomObj.first, nullptr);
            --numObjectPerBucket[kickRegionId][curBid];
            int64_t anotherBid = (curBid == getZNSKVCacheBucketId(randomObj.first)) 
                                            ? getZNSKVCacheBucketId2(randomObj.first) 
                                            : getZNSKVCacheBucketId(randomObj.first);
            q.push({anotherBid, randomObj}); 
        }

        RegionBucketStorage::Allocation alloc = dataRegion[kickRegionId][curBid]->allocate(obj.first, obj.second);
        dataRegion[kickRegionId][curBid]->insert(alloc, obj.first, obj.second); 
        int kickRegionOffset = kickRegionId == curDataRegionId ? 0 : 1;
        bfRebuild(curBid, dataRegion[kickRegionId][curBid], kickRegionOffset);
        ++numKicks;
        ++numObjectPerBucket[kickRegionId][curBid];
        
      
        q.pop();
        if (numKicks >= maxNumKicks) { 
            flushFlag = true;
            break;
        }
    }
    return flushFlag;
}

Status ZNSKVCache::lookup(HashedKey hk, Buffer& value) {
    // get bid
    uint32_t bid1 = getZNSKVCacheBucketId(hk);
    uint32_t bid2 = getZNSKVCacheBucketId2(hk);
    lookupCount_.inc();
    
    // bid1
    Status result = subLookup(bid1, hk, value);
    statHitBid_[bid1]->inc();

    // bid2
    if (result == Status::NotFound && isEnableDoubleHash) {
        result = subLookup(bid2, hk, value);
        statHitBid_[bid2]->inc();
    }

    if (result == Status::Ok) {
        succLookupCount_.inc();
    }
    return result;
}

Status ZNSKVCache::subLookup(int64_t bid, HashedKey hk, Buffer& value) {
    // 1.if in memory dataRegion
    // 1.1 find in primary dataRegion
    {
        std::shared_lock<std::shared_mutex> lock(buckets[bid]);//read shared_mutex

        int16_t ret = scanBucket(dataRegion[curDataRegionId][bid], hk, value);

        // 1.2 if enable [Double Buffer], scan the secondary dataRegion to find out if there is a newer version
        if (isEnableDoubleBuffer) {
            int16_t ret2 = scanBucket(dataRegion[SECONDARY_DATAREGION][bid], hk, value);
            if (ret2 != -1) ret = ret2;
        }
    
        if (ret != -1) {
            // XLOG(INFO) << "Lookup successfully in memory dataRegion";
            return Status::Ok;
        }
    }
    // 2.else, find in ZNS ssd (to find zones which may contain KV)
    return lookupInSSD(bid, hk, value);
}

Status ZNSKVCache::lookupInSSD(int64_t bid, HashedKey hk, Buffer& value) {
    // get corresponding bucket BFs ==> get potential zones ==> confirm existence by scanBucket
    // (1) get corresponding bucket BFs
    // (1.1) hot
    lookupInSSDCount_.inc();
    statHitNvmBid_[bid]->inc();
    std::shared_ptr<PBFG> pbfg;
    Status isGetHotPBFG;
    {
        std::shared_lock<std::shared_mutex> lock(pbfgMutex);
        isGetHotPBFG = pbfgList->getPBFG(bid, pbfg);
    }
    if (isGetHotPBFG == Status::NotFound) {
        PBFGListHitMiss_.inc();
        pbfg = getPBFGFromDevice(bid);
    }
    // (1.2) cold
    // (2) get potential zones by pbfg
    std::vector<int32_t> zoneIdList(0);
    calZoneId(bid, hk, pbfg, zoneIdList);

    // (3) confirm existence by scanBucket
    bool found = false;
    for (int32_t i = zoneIdList.size() - 1; i >= 0; --i) {
        int32_t zoneId = zoneIdList[i];
        auto buffer = readDataZoneBucketBuffer(bid, zoneId);
        XDCHECK(!buffer.isNull());
        auto* bucket = reinterpret_cast<RegionBucket*>(buffer.data());
        int16_t objId = scanBucket(bucket, hk, value);
        if (objId != -1) {
            bool canWarmUp = dataPool->isNeedStat(zoneId, startStatZone);
            if (writeBackMode && dataPool->isNeedStat(zoneId, startStatZone)) {
                // if lookup hit, warm up
                visitedMask[zoneId - dataZoneOffsetId][bid] = 
                visitedMask[zoneId - dataZoneOffsetId][bid] | (1 << objId);
            }
            found = true;
            break;
        }
    }
    if (isGetHotPBFG == Status::NotFound) {
        std::unique_lock<std::shared_mutex> lock(pbfgMutex);
        pbfgList->push(bid, pbfg);
    }

    return found ? Status::Ok : Status::NotFound;
}

Status ZNSKVCache::remove(HashedKey hk) {
    removeCount_.inc();
    return Status::Ok;
}

void ZNSKVCache::flush() {
    device.flush();
}

uint64_t ZNSKVCache::getMaxItemSize() const {
  // does not include per item overhead
  return bucketSize - sizeof(RegionBucket);
}

void ZNSKVCache::getCounters(const CounterVisitor& visitor) const {
  visitor("navy_bh_items", itemCount_.get());
  visitor("navy_bh_inserts", insertCount_.get());
  visitor("navy_bh_succ_inserts", succInsertCount_.get());
  visitor("navy_bh_lookups", lookupCount_.get());
  visitor("navy_bh_succ_lookups", succLookupCount_.get());
  visitor("navy_bh_removes", removeCount_.get());
  visitor("navy_bh_succ_removes", succRemoveCount_.get());
  visitor("navy_bh_evictions", evictionCount_.get());
  visitor("navy_bh_logical_written", logicalWrittenCount_.get());
  visitor("navy_bh_read", readCount_.get());
}

void ZNSKVCache::persist(RecordWriter& rw) {
  XLOG(INFO, "Starting ZNSKVCache persist");
  XLOG(INFO, "Finished ZNSKVCache persist");
}

bool ZNSKVCache::recover(RecordReader& rr) {
  XLOG(INFO, "Starting ZNSKVCache recovery");
  XLOG(INFO, "Finished ZNSKVCache recovery");
  return true;
}

void ZNSKVCache::coldAllStepZones() {
   
    for (int i = (dataPool->getWritedZoneNum() - startStatZone - step); i > 0; i -= step) {
        auto coldZoneId = dataPool->getZoneStartId() + i;
        if (coldZoneId > dataPool->getZoneLastId())
            coldZoneId -= dataPool->getSize();
        coldZone(coldZoneId);
    }
}

void ZNSKVCache::coldZone(int32_t zoneId) {
    for(int32_t bid = 0; bid < numBuckets; ++bid) {
        if(!pbfgList->containsBid(bid)) {
            visitedMask[zoneId - dataZoneOffsetId][bid] = 0;
        }
    }
}

RegionBucket* ZNSKVCache::readDataZoneBucket(int32_t bid, int32_t euId) {
    int64_t loc = getEuIdLoc(euId, bid);
    bufferTmp = device.makeIOBuffer(bucketSize);
    XDCHECK(!bufferTmp.isNull());
    const bool res = device.read(loc, bufferTmp.size(), bufferTmp.data());
    if (!res) {
        return nullptr;
    }
    RegionBucket* bucket = reinterpret_cast<RegionBucket*>(bufferTmp.data());
    return bucket;
}

Buffer ZNSKVCache::readDataZoneBucketBuffer(int32_t bid, int32_t euId) {
    int64_t loc = getEuIdLoc(euId, bid);    
    Buffer buffer = device.makeIOBuffer(bucketSize);
    XDCHECK(!buffer.isNull());
    const bool res = device.read(loc, buffer.size(), buffer.data());
    readSSDCount_.inc();
    readCount_.inc();
    if (!res) {
        return {};
    }
    return buffer;
}

int16_t ZNSKVCache::scanBucket(RegionBucket* bucket, HashedKey hk, Buffer& value){
    RegionBucket::Iterator itr = bucket->getFirst();
    int16_t objId = 0;
    while (!itr.done()) {
        HashedKey objecthk = itr.hashedKey();
        std::unique_ptr<BufferView> objectvalue = std::make_unique<BufferView>(itr.value());
        if (objecthk == hk) {
            value = Buffer(*objectvalue);
            return objId;
        }
        itr = bucket->getNext(itr);
        objId++;
    }
    return -1;
}

bool ZNSKVCache::ifHkEqual(HashedKey hk, HashedKey other) {
    if (hk.keyHash() == other.keyHash())    
        return true;
    if (isEnableDoubleHash && (hk.keyHash2() == other.keyHash2() 
        || hk.keyHash() == other.keyHash2() || hk.keyHash2() == other.keyHash()))
        return true;
    return false;
}

void ZNSKVCache::readMetaDataRegionBucket(HashedKey hk) {
    int32_t bid = getZNSKVCacheBucketId(hk);
    bool ret1 = metaDataRegion[curMetaDataRegionId][bid].couldExist(0, hk.keyHash());

    int64_t loc = getEuIdLoc(51, bid);
    Buffer buffer = device.read(loc, perRowPBFByteSize);
    readSSDCount_.inc();
    XDCHECK(!buffer.isNull());
}

std::shared_ptr<PBFG> ZNSKVCache::getPBFGFromDevice(int32_t bid) {
    if (metaDataPool->isEmpty()) {
        return std::make_shared<PBFG>();
    }
    
    std::shared_ptr<PBFG> pbfg = std::make_shared<PBFG>(); 
    int32_t startDataZoneId = metaDataPool->getZoneStartId();
    int32_t endDataZoneId = metaDataPool->getZoneEndId();
    int32_t writedMetaZoneNum = metaDataPool->getWritedZoneNum();
    int32_t size = metaDataPool->getSize();
    int32_t lastZoneId = metaDataPool->getZoneLastId();

    for (int32_t i = 0; i < writedMetaZoneNum; ++i) {
        int32_t zoneId = startDataZoneId + i;
        zoneId = zoneId > lastZoneId ? (zoneId - size) : zoneId;
        // XLOG(INFO) << "[getPBFGFromDevice] meta zoneId: " << zoneId;
        int64_t loc = getEuIdLoc(zoneId, bid);
        Buffer buffer = device.read(loc, perRowPBFByteSize);
        readSSDCount_.inc();
        readCount_.inc();
        XDCHECK(!buffer.isNull());
        
        BloomFilter bf = BloomFilter::makeBloomFilter(METADATARATIO, elementCount, fpProb);
        bf.setBitArray(buffer);
        std::unique_lock<std::shared_mutex> lock(pbfgMutex);
        pbfg->flushOnePBF(bf);
        
    }

    return pbfg;
}

int32_t ZNSKVCache::getMask() {
    return numEvictZones;
}

void ZNSKVCache::calZoneId(int32_t bid, HashedKey hk, std::shared_ptr<PBFG> pbfg, 
                            std::vector<int32_t>& zoneIdList) {
    std::vector<uint64_t> hashAddress_;
    metaDataRegion[curMetaDataRegionId][bid].getAllHashIdx(hk.keyHash(), hashAddress_);

    if (pbfg) {
        std::shared_lock<std::shared_mutex> lock(pbfgMutex);

        int32_t size = pbfg->getRowPBFQueue().size(); 
        for(int32_t rowPBFId = 0; rowPBFId < size; ++rowPBFId) {         
            auto& rowPBF = pbfg->getRowPBFQueue()[rowPBFId];   
            for(int32_t cycleIdx = ((rowPBFId == 0) ? numEvictZones : 0); cycleIdx < METADATARATIO; ++cycleIdx) {
                if(rowPBF.existObj(cycleIdx, hk.keyHash(), hashAddress_)) {  
                    int32_t hitZoneId = dataPool->getZNSHitZoneId(rowPBFId, cycleIdx, METADATARATIO);
                    zoneIdList.push_back(hitZoneId);
                }
            }
        }
    }
    
    for(int32_t memPBFidx = 0; memPBFidx < numFlushZones; ++memPBFidx) { 
        std::shared_lock<std::shared_mutex> lock(buckets[bid]); 

        if(metaDataRegion[curMetaDataRegionId][bid].existObj(memPBFidx, hk.keyHash(), hashAddress_)) {    
            int32_t hitZoneId = dataPool->getMemHitZoneId(numFlushZones, memPBFidx);
            zoneIdList.push_back(hitZoneId);
        }
    }
}

void ZNSKVCache::metaDataRegionSet(HashedKey hk, int32_t bid, int8_t regionOffset) {
    int32_t zoneId = (numFlushZones + regionOffset) % METADATARATIO; 
    metaDataRegion[curMetaDataRegionId][bid].set(zoneId, hk.keyHash());
}

void ZNSKVCache::bfRebuild(uint32_t bid, const RegionBucket* bucket, uint32_t kickRegionOffset) {
    XDCHECK(bucket);
    uint32_t zoneId = (kickRegionOffset + numFlushZones) % METADATARATIO;
    metaDataRegion[curMetaDataRegionId][bid].clear(zoneId);
    auto itr = bucket->getFirst();
    uint32_t i = 0;
    uint32_t total = bucket->size();
    while (!itr.done() && i < total) {
      if (i >= total) {
        XLOGF(INFO, "Bucket {}: has only {} items, iterating through {}, not done {}",
            bid, total, i, itr.done());
      }
      metaDataRegion[curMetaDataRegionId][bid].set(zoneId, itr.keyHash());
      itr = bucket->getNext(itr);
      i++;
    }
  }

//test zone 1
void ZNSKVCache::metaDataRegionSet2(int64_t key, int32_t bid) {
    int32_t zoneId = folly::Random::rand64() % METADATARATIO;
    metaDataRegion[curMetaDataRegionId][bid].set(zoneId, key);
    auto bfsize = metaDataRegion[curMetaDataRegionId][bid].getByteSize();
    auto bitarray = metaDataRegion[curMetaDataRegionId][bid].getBitArray();
    for (int i = 0; i < bfsize; ++i) {
        if (bitarray[i]) XLOG(INFO) << "bitarray[i]:" << bitarray[i];
    }
}

void ZNSKVCache::writeBack(int32_t zoneId) {
    for (int32_t bid = 0; bid < numBuckets; bid++) {
        if (pbfgList->containsBid(bid)) {
     
            auto buffer = readDataZoneBucketBuffer(bid, zoneId);
            XDCHECK(!buffer.isNull());
            auto* bucket = reinterpret_cast<RegionBucket*>(buffer.data());
            assert(bucket); // bucket shouldn't be null
            if (!bucket) continue;
            RegionBucket::Iterator itr = bucket->getFirst();
            int16_t j = 0; 
            bool coulInsert = true;
            while (!itr.done() && coulInsert) {
                HashedKey objecthk = itr.hashedKey();
                BufferView objectvalue = itr.value();
                if (!dataRegion[SECONDARY_DATAREGION][bid]->isSpace(objecthk, objectvalue)) {
                    coulInsert = false;
                    break;
                }
                if ((visitedMask[zoneId - dataZoneOffsetId][bid] >> j) & 1 ) {
                    // write back
                    reInsert(objecthk, objectvalue, bid);
      
                }
                itr = bucket->getNext(itr);
                ++j;
            }

            itr = bucket->getFirst();
            j = 0; 
            while (!itr.done() && coulInsert) {
                HashedKey objecthk = itr.hashedKey();
                BufferView objectvalue = itr.value();
                if (!dataRegion[SECONDARY_DATAREGION][bid]->isSpace(objecthk, objectvalue)) {
                    coulInsert = false;
                    break;
                }
                auto random = folly::Random::randDouble01();
                if (!((visitedMask[zoneId - dataZoneOffsetId][bid] >> j) & 1)) {
                    // write back
                    reInsert(objecthk, objectvalue, bid);
                }
                itr = bucket->getNext(itr);
                ++j;
            }
        }
    }
    // reset all mask of flushed zone
    std::memset(visitedMask[zoneId  - dataZoneOffsetId].get(), 0, numBuckets * sizeof(int64_t));
}

bool ZNSKVCache::reInsert(HashedKey hk, BufferView value, int32_t bid) {
    { 

        RegionBucketStorage::Allocation alloc = dataRegion[SECONDARY_DATAREGION][bid]->allocate(hk, value);
        dataRegion[SECONDARY_DATAREGION][bid]->insert(alloc, hk, value);
        numObjectPerBucket[SECONDARY_DATAREGION][bid]++;
        metaDataRegionSet(hk, bid, SECONDARY_DATAREGION);
        itemCount_.inc();
        reInsertCount_.add(hk.key().size() + value.size());
    }
}

void ZNSKVCache::evictZone(std::unique_ptr<ZonePool>& pool, bool poolType) {
    std::unique_lock<std::shared_mutex> lock(evictMutex);

    int32_t evictEuId = pool->getZoneStartId();
    
    pool->dequeue();
    
    // if poolType is dataPool 
    if (writeBackMode && poolType == DATAMODE) {
        writeBack(evictEuId);
    }

    // update only when evicting metaDataZone
    if (poolType == METADATAMODE) {
        std::unique_lock<std::shared_mutex> lock(pbfgMutex);

        pbfgList->evictChangeAllPBFG();
        dataPool->changePoolCycleStartId(METADATARATIO);
    }

    int64_t loc = getEuIdLoc(evictEuId, 0);
    device.reset(loc, euCap);
    // sleep(5);
}

ZNSKVCache::~ZNSKVCache() {
}

} // namespace navy
} // namespace cachelib
} // namespace facebook