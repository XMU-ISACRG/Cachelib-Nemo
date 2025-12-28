#pragma once

#include <functional>

#include <folly/Portability.h>

#include "cachelib/navy/znskvcache/RegionBucketStorage.h"
#include "cachelib/navy/znskvcache/Type.h"
#include "cachelib/navy/common/Buffer.h"
#include "cachelib/navy/common/Hash.h"
#include "cachelib/navy/common/Types.h"

namespace facebook {
namespace cachelib {
namespace navy {

class FOLLY_PACK_ATTR RegionBucket {
 public:
  // Iterator to bucket's items.
  class Iterator {
   public:
    bool done() const { return itr_.done(); }

    BufferView key() const;
    HashedKey hashedKey() const;
    uint64_t keyHash() const;
    BufferView value() const;

    bool keyEqualsTo(HashedKey hk) const;
    bool keyEqualsTo(uint64_t keyHash) const;

   private:
    friend RegionBucket;

    Iterator() = default;
    explicit Iterator(RegionBucketStorage::Allocation itr) : itr_{itr} {}

    RegionBucketStorage::Allocation itr_; //alloc
  };

  // User will pass in a view that contains the memory that is a KangarooBucket
  static uint32_t computeChecksum(BufferView view);

  // Initialize a brand new RegionBucket given a piece of memory in the case
  // that the existing bucket is invalid. (I.e. checksum or generation
  // mismatch). Refer to comments at the top on what do we use checksum
  // and generation time for.
  static RegionBucket& initNew(MutableBufferView view, uint64_t generationTime);

  uint32_t getChecksum() const { return checksum_; }

  void setChecksum(uint32_t checksum) { checksum_ = checksum; }

  uint64_t generationTime() const { return generationTime_; }

  uint32_t size() const { return storage_.numAllocations(); }
  uint32_t capacity() const { return storage_.capacity(); }
  uint32_t remainingCapacity() const { return storage_.remainingCapacity(); }
  double fillRatio() const { return (double)(storage_.capacity() - storage_.remainingCapacity()) / storage_.capacity(); }

  // Look up for the value corresponding to a key.
  // BufferView::isNull() == true if not found.
  BufferView find(HashedKey hk) const;

  int16_t findByHk(HashedKey hk, Buffer& value) const;

  BufferView findTag(uint32_t tag, HashedKey& hk) const;

  void getEveryObjFromBucket();

  uint32_t insert(HashedKey hk,
                  BufferView value,
                  const DestructorCallback& destructorCb);

  // Remove an entry corresponding to the key. If found, invoke @destructorCb
  // before returning true. Return number of entries removed.
  uint32_t remove(HashedKey hk, const DestructorCallback& destructorCb);

  // remove the first obj
  uint32_t removeFirst();

  // Reorders entries in bucket based on NRU bit vector callback results
  void reorder(BitVectorReadVisitor isHitCallback);

  // Needed for log buckets, allocate does not remove objects
  bool isSpace(HashedKey hk, BufferView value);

  // if there is enough space without exceeding the threshold
  bool ifNotReachBucketThreshold(HashedKey hk, BufferView value, int16_t flushThreshold);

  RegionBucketStorage::Allocation allocate(HashedKey hk, BufferView value);
  void insert(RegionBucketStorage::Allocation alloc, HashedKey hk, BufferView value);
  void clear();

  Iterator getFirst() const;
  Iterator getNext(Iterator itr) const;

  uint32_t remainCapacity();

  uint32_t capacity() {return storage_.capacity();}

  uint32_t computeKVSize(HashedKey hk,BufferView value);

  uint32_t slotSize();

  uint32_t space() {return sizeof(RegionBucket);}

 private:
  RegionBucket(uint64_t generationTime, uint32_t capacity)
      : generationTime_{generationTime}, storage_{capacity} {}

  // Reserve enough space for @size by evicting. Return number of evictions.
  uint32_t makeSpace(uint32_t size, const DestructorCallback& destructorCb);

  uint32_t checksum_{};
  uint64_t generationTime_{};
  RegionBucketStorage storage_; 
};
} // namespace navy
} // namespace cachelib
} // namespace facebook