#pragma once

#include "cachelib/common/BloomFilter.h"
#include "cachelib/navy/znskvcache/RegionBucket.h"
#include "cachelib/navy/znskvcache/ZonePool.h"
#include "cachelib/navy/znskvcache/Types.h"
#include "cachelib/navy/common/Device.h"
#include "cachelib/navy/common/Buffer.h"
#include "cachelib/navy/Factory.h"
#include "cachelib/navy/znskvcache/PBFG.h"
#include <iostream>

namespace facebook {
namespace cachelib {
namespace navy {

class PBFGList{
public:
    PBFGList(int32_t capacity) : capacity_(capacity) {}

    Status getPBFG(const int32_t& offset, std::shared_ptr<PBFG>& pbfg);

    void pop();

    void push(const int32_t& offset, std::shared_ptr<PBFG> pbfg);

    void evictChangeAllPBFG();

    void flushChangeAllPBFG(std::unique_ptr<BloomFilter[]>& flushMetaDataRegion);

    bool containsBid(int32_t bid) const {
        return pbfgMap_.find(bid) != pbfgMap_.end();
    }

    std::list<std::pair<int32_t, std::shared_ptr<PBFG>>>& 
                            getList() {return pbfgList_;}

    std::unordered_map<int32_t, std::list<std::pair<int32_t, std::shared_ptr<PBFG>>>::iterator>& 
                            getMap() {return pbfgMap_;}

private:
    
    int32_t capacity_;
    std::unordered_map<int32_t, std::list<std::pair<int32_t, std::shared_ptr<PBFG>>>::iterator> pbfgMap_;
    std::list<std::pair<int32_t, std::shared_ptr<PBFG>>> pbfgList_;
};

} // namespace navy
} // namespace cachelib
} // namespace facebook

