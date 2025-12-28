#include "ZonePool.h"

namespace facebook{
namespace cachelib{
namespace navy{
bool ZonePool::isFull() {
    return (zoneNextId_ + 1) % size_ == zoneStartId_;
}

bool ZonePool::isEmpty() {
    return zoneNextId_ == zoneStartId_;
}

void ZonePool::enqueue() {
    if(!ZonePool :: isFull()){
        zoneNextId_ = (zoneNextId_ + 1) % size_;
    } else {
        throw std::overflow_error(
            folly::sformat("Pool is full, can't open zone"));
    }
}

void ZonePool::dequeue() {
    if(!ZonePool :: isEmpty()){
        zoneStartId_ = (zoneStartId_ + 1) % size_;
    } else {
        throw std::overflow_error(
            folly::sformat("Pool is empty, can't reset zone"));
    }
}

bool ZonePool::isNeedStat(int32_t zoneId, int32_t startStatZone) {
    return (zoneId - zoneOffsetId + startStatZone) % size_ <= getZoneEndId();
}


}//namespace navy
}//namespace cachelib
}//namespace facebook