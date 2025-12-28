#pragma once

#include <queue>
#include "cachelib/common/BloomFilter.h"
#include "cachelib/navy/znskvcache/ZonePool.h"

namespace facebook {
namespace cachelib {
namespace navy {

class PBFG {
public:
    // enqueue -> RowPBFQueue
    void flushOnePBF(BloomFilter& RowPBF);
    // void flushOnePBF(BloomFilter RowPBF);

    // dequeue -> RowPBFQueue
    void evictOnePBF();

    // get RowPBFQueue
    std::deque<BloomFilter>& getRowPBFQueue() {
        return RowPBFQueue;
    }

private:
    std::deque<BloomFilter> RowPBFQueue;

};

} // namespace navy
} // namespace cachelib
} // namespace facebook