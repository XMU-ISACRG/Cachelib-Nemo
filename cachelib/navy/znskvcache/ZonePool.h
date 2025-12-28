#pragma once

#include<iostream>
#include <folly/Format.h>
#include "cachelib/navy/common/Device.h"
#include "cachelib/navy/znskvcache/Types.h"

namespace facebook{
namespace cachelib{
namespace navy{
//ZonePool:record the start id,end id,the number of zones,and the size of the zone
//@zoneStartId_    id of the startZone;
//@zoneNextId_     next id of the lastZone;
//@size_           the max size of pool
//@numZone_        the nums of element

class ZonePool{
public:
//queue with startid and capacity.
ZonePool(int32_t zoneOffsetId, int32_t size)
        : zoneStartId_(0), zoneNextId_(0), poolCycleStartId(0), size_(size + 1), zoneOffsetId(zoneOffsetId) {}

int32_t getZoneStartId() { return zoneStartId_ + zoneOffsetId; }

int32_t getZoneNextId() { return zoneNextId_ + zoneOffsetId; }

int32_t getZoneEndId() { return (zoneNextId_ - 1 + size_) % size_ + zoneOffsetId; }

int32_t getWritedZoneNum() { return (zoneNextId_ - zoneStartId_ + size_) % size_; }

int32_t getZoneLastId() { return size_ - 1 + zoneOffsetId; }

int32_t getZNSHitZoneId(int32_t rowPBFId, int32_t cycleIdx, int32_t METADATARATIO) {
    return (poolCycleStartId + rowPBFId * METADATARATIO + cycleIdx) % size_ + zoneOffsetId;}

void changePoolCycleStartId(int32_t METADATARATIO) {
    poolCycleStartId = (poolCycleStartId + METADATARATIO) % size_;}

int32_t getMemHitZoneId(int32_t numFlushZones, int32_t memPBFidx) {
    return (zoneNextId_ - numFlushZones + memPBFidx + size_) % size_ + zoneOffsetId;}

int32_t getSize() { return size_; }

int32_t getZoneOffset() { return zoneOffsetId; }

bool isNeedStat(int32_t zoneId, int32_t startStatZone);

bool isFull();

bool isEmpty();

void enqueue();

void dequeue();
    
private:
    int32_t zoneStartId_;   
    int32_t zoneNextId_;    
    int32_t poolCycleStartId;   

    int32_t size_;
    int32_t zoneOffsetId;   
};

}//namespace navy
}//namespace cachelib
}//namespace facebook