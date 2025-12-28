#include "cachelib/navy/kangaroo/KangarooBucketStorage.h"
#include "cachelib/navy/kangaroo/FwLogSegment.h"

namespace facebook {
namespace cachelib {
namespace navy {

FwLogSegment::FwLogSegment(uint64_t segmentSize, 
      uint64_t pageSize, LogSegmentId lsid, uint32_t numPartitions,
      MutableBufferView mutableView, bool newBucket)
  : allocationMutexes_{new folly::SharedMutex[numPartitions]}, 
	  segmentSize_{segmentSize},
    pageSize_{pageSize},
    numBuckets_{segmentSize_ / pageSize_},
		numPartitions_{numPartitions},
    bucketsPerPartition_{numBuckets_ / numPartitions_},
    lsid_{lsid},
    buckets_{new LogBucket*[numBuckets_]} {
  // initialize all of the Kangaroo Buckets after cast
  for (uint64_t i = 0; i < numBuckets_; i++) {
    // TODO: fix generation time
    uint64_t offset = i * pageSize_;
    auto view = MutableBufferView(pageSize_, mutableView.data() + offset);
    if (newBucket) {
      LogBucket::initNew(view, 0);
    }
    buckets_[i] = reinterpret_cast<LogBucket*>(mutableView.data() + offset);
  }
}

FwLogSegment::~FwLogSegment() {
	delete[] allocationMutexes_;
	delete buckets_;
}

BufferView FwLogSegment::find(HashedKey hk, LogPageId lpid) {
  uint32_t offset = bucketOffset(lpid);
  XDCHECK(offset < numBuckets_);
  return buckets_[offset]->find(hk);
}

BufferView FwLogSegment::findTag(uint32_t tag, HashedKey& hk, LogPageId lpid) {
  uint32_t offset = bucketOffset(lpid);
  XDCHECK(offset < numBuckets_);
  return  buckets_[offset]->findTag(tag, hk);
}

int32_t FwLogSegment::insert(HashedKey hk, BufferView value, uint32_t partition) {
  XDCHECK(partition < numPartitions_);
  KangarooBucketStorage::Allocation alloc;
  uint32_t i = bucketsPerPartition_ * partition;
  uint32_t endOffset = i + bucketsPerPartition_;
  bool foundAlloc = false;
  {
    std::unique_lock<folly::SharedMutex> lock{allocationMutexes_[partition]};
    // not necessarily the best online bin packing heuristic
    // could potentially also do better sharding which segment
    // to choose for performance reasons depending on bottleneck
    for (; i < endOffset;  i++) {
      if (buckets_[i]->isSpace(hk, value)) {
        alloc = buckets_[i]->allocate(hk, value);
        foundAlloc = true;
        break;
      }
    }
  }
  if (!foundAlloc) {
    return -1;
  }
  // space already reserved so no need to hold mutex
  buckets_[i]->insert(alloc, hk, value);
  return i;
}
  
uint32_t FwLogSegment::bucketOffset(LogPageId lpid) {
  return lpid.index() % numBuckets_;
}

LogSegmentId FwLogSegment::getLogSegmentId() {
  return lsid_;
}

void FwLogSegment::clear(LogSegmentId newLsid) {
  lsid_ = newLsid;
  for (uint64_t i = 0; i < numBuckets_; i++) {
    buckets_[i]->clear();
  }
}
  
FwLogSegment::Iterator* FwLogSegment::getFirst() {
  auto itr = buckets_[0]->getFirst();
  return new Iterator(0, itr, bucketsPerPartition_);
}

FwLogSegment::Iterator* FwLogSegment::getNext(Iterator* itr) {
  if (itr->done()) {
    return itr;
  }
  auto nextItr = buckets_[itr->bucketNum_]->getNext(itr->itr_);
  if (nextItr.done() && (itr->bucketNum_ + 1) % (bucketsPerPartition_ * numPartitions_) == 0) {
    itr->done_ = true;
    return itr;
  } else if (nextItr.done()) {
    itr->bucketNum_++;
    itr->itr_ = buckets_[itr->bucketNum_]->getFirst();
    if (itr->itr_.done()) {
      return getNext(itr);
    }
    return itr;
  } else {
    itr->itr_ = nextItr;
    return itr;
  }
}

double FwLogSegment::getFullness(uint32_t partition) {
  uint32_t i = bucketsPerPartition_ * partition;
	uint32_t endOffset = i + bucketsPerPartition_;
	uint32_t totalSize = 0;
  uint32_t remainingSize = 0;
	for (; i < endOffset;  i++) {
		totalSize += buckets_[i]->capacity();
    remainingSize += buckets_[i]->remainingCapacity();
	}
	return (totalSize - remainingSize) / double(totalSize);
}

// return objects Num in the logSegment
uint64_t FwLogSegment::getObjNum() {
    FwLogSegment::Iterator* itr = getFirst();
    uint64_t objNum = 0;
    while (!itr->done()) {
      ++objNum;
      itr = getNext(itr);
    }
    return objNum;
  }

// return total size of objects in the logSegment
uint64_t FwLogSegment::getSize() {
  uint64_t totalSize = 0;
  FwLogSegment::Iterator* itr = getFirst();
  while (!itr->done()) {
    totalSize += itr->key().key().size() + itr->value().size();
    itr = getNext(itr);
  }
  return totalSize;
}

} // namespace navy
} // namespace cachelib
} // namespace facebook
