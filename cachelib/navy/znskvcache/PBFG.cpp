#include "PBFG.h"

namespace facebook {
namespace cachelib {
namespace navy {

void PBFG::flushOnePBF(BloomFilter& RowPBF) {
    // RowPBFQueue.emplace_back(std::move(RowPBF));
    RowPBFQueue.push_back(std::move(RowPBF));
}

void PBFG::evictOnePBF() {
    RowPBFQueue.pop_front();
}

} // namespace navy
} // namespace cachelib
} // namespace facebook