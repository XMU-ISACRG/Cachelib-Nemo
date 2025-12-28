#include "cachelib/navy/znskvcache/PBFGList.h"

namespace facebook {
namespace cachelib {
namespace navy {

Status PBFGList::getPBFG(const int32_t& offset, std::shared_ptr<PBFG>& pbfg) {
    auto it = pbfgMap_.find(offset);
    if (it == pbfgMap_.end()) {
        return Status::NotFound; 
    }
    if (it->second == pbfgList_.end()) { 
        return Status::NotFound;
    }

    pbfg = it->second->second;
    return Status::Ok;
}

void PBFGList::pop(){
    if (pbfgList_.empty()) {
        throw std::runtime_error("Cannot pop from an empty PBFGList.");
    }
    int32_t offset_to_remove = pbfgList_.front().first;
    pbfgMap_.erase(offset_to_remove); 
    pbfgList_.pop_front(); 
}

void PBFGList::push(const int32_t& offset, std::shared_ptr<PBFG> pbfg) {
    if (pbfgList_.size() == capacity_) {
        pop();
    }
    pbfgList_.emplace_back(offset, std::move(pbfg)); 
    pbfgMap_[offset] = --pbfgList_.end();
}

void PBFGList::evictChangeAllPBFG() {
    for(auto& i : pbfgList_) {
        i.second->evictOnePBF();
    }
}

void PBFGList::flushChangeAllPBFG(std::unique_ptr<BloomFilter[]>& flushMetaDataRegion) {
    for(auto& i : pbfgList_) {
        BloomFilter rowPBF(flushMetaDataRegion[i.first]);
        i.second->flushOnePBF(rowPBF);
    }
}

}//navy
}//cachelib 
}//facebook 