#include <vector>
#include <utility>

#include "cachelib/navy/znskvcache/RegionBucket.h"
#include "cachelib/navy/common/Hash.h"
#include <iostream>

namespace facebook {
namespace cachelib {
namespace navy {
static_assert(sizeof(RegionBucket) == 24,
              "RegionBucket overhead. If this changes, you may have to adjust the "
              "sizes used in unit tests.");

namespace {
// This maps to exactly how an entry is stored in a bucket on device.
class FOLLY_PACK_ATTR RegionBucketEntry {
 public:
  static uint32_t computeSize(uint32_t keySize, uint32_t valueSize) {

    return sizeof(RegionBucketEntry) + keySize + valueSize;
  }

  static RegionBucketEntry& create(MutableBufferView storage,
                             HashedKey hk,
                             BufferView value) {
    new (storage.data()) RegionBucketEntry{hk, value};

    return reinterpret_cast<RegionBucketEntry&>(*storage.data());
  }

  BufferView key() const { return {keySize_, data_}; }
  
  HashedKey hashedKey() const {
    return HashedKey::precomputed(toStringPiece(key()), keyHash_);
  }
  
  uint64_t keyHash() const { return makeHK(key()).keyHash(); }

  bool keyEqualsTo(HashedKey hk) const {
    return hk == makeHK(key());
  }
  
  bool keyEqualsTo(uint64_t hash) const {
    return hash == keyHash();
  }

  BufferView value() const {
    if (this == nullptr) {
      throw std::runtime_error("RegionBucketEntry::value() called on nullptr");
    }
    return {valueSize_, data_ + keySize_}; }

 private:
  RegionBucketEntry(HashedKey hk, BufferView value)
      : keySize_{static_cast<uint16_t>(hk.key().size())},
        valueSize_{static_cast<uint16_t>(value.size())},
        keyHash_{hk.keyHash()} {
    static_assert(sizeof(RegionBucketEntry) == 12, "RegionBucketEntry overhead");
    makeView(hk.key()).copyTo(data_);
    value.copyTo(data_ + keySize_); 
  }

  const uint16_t keySize_{};
  const uint16_t valueSize_{};
  const uint64_t keyHash_{};
  uint8_t data_[];  
};
const RegionBucketEntry* getIteratorEntry(RegionBucketStorage::Allocation itr) {
  return reinterpret_cast<const RegionBucketEntry*>(itr.view().data());
}
} // namespace

BufferView RegionBucket::Iterator::key() const {
  return getIteratorEntry(itr_)->key();
}

HashedKey RegionBucket::Iterator::hashedKey() const {
  return getIteratorEntry(itr_)->hashedKey();
}

uint64_t RegionBucket::Iterator::keyHash() const {
  return getIteratorEntry(itr_)->keyHash();
}

BufferView RegionBucket::Iterator::value() const {
  return getIteratorEntry(itr_)->value();
}

bool RegionBucket::Iterator::keyEqualsTo(HashedKey hk) const {
  return getIteratorEntry(itr_)->keyEqualsTo(hk);
}

bool RegionBucket::Iterator::keyEqualsTo(uint64_t keyHash) const {
  return getIteratorEntry(itr_)->keyEqualsTo(keyHash);
}

uint32_t RegionBucket::computeChecksum(BufferView view) {
  constexpr auto kChecksumStart = sizeof(checksum_);
  auto data = view.slice(kChecksumStart, view.size() - kChecksumStart);
  return navy::checksum(data);
}

RegionBucket& RegionBucket::initNew(MutableBufferView view, uint64_t generationTime) {
  return *new (view.data())
      RegionBucket(generationTime, view.size() - sizeof(RegionBucket)); 
}

BufferView RegionBucket::find(HashedKey hk) const {
  auto itr = storage_.getFirst();
  uint32_t keyIdx = 0;
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    if (entry->keyEqualsTo(hk)) {
      return entry->value();
    }
    itr = storage_.getNext(itr);
    keyIdx++;
  }
  return {};
}

int16_t RegionBucket::findByHk(HashedKey hk, Buffer& value) const {
  auto itr = storage_.getFirst();
  int16_t keyIdx = 0;
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    std::cout << "entry->key(): " << toString(entry->key()) << std::endl;
    if (entry->keyEqualsTo(hk)) {
      return keyIdx;
    }
    itr = storage_.getNext(itr);
    keyIdx++;
  }
  return -1;
}

BufferView RegionBucket::findTag(uint32_t tag, HashedKey& hk) const {
  auto itr = storage_.getFirst();
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    hk = makeHK(entry->key());
    if (createTag(hk) == tag) {
      return entry->value();
    }
    itr = storage_.getNext(itr);
  }
  return {};
}

void RegionBucket::getEveryObjFromBucket() {
  XLOG(INFO) << "===RegionBucket::getEveryObjFromBucket===";
  auto itr = storage_.getFirst();
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    
    const auto size = RegionBucketEntry::computeSize(entry->key().size(), entry->value().size());
    const auto requiredSize = RegionBucketStorage::slotSize(size);
    XLOG(INFO) << "obj size(key.size + value.size): " << size;
    XLOG(INFO) << "obj total size(key.size + value.size + DataStructure.size): " << requiredSize;

    itr = storage_.getNext(itr);
  }
}

uint32_t RegionBucket::insert(HashedKey hk,
                        BufferView value,
                        const DestructorCallback& destructorCb) {
  const auto size = RegionBucketEntry::computeSize(hk.key().size(), value.size());
  XDCHECK_LE(size, storage_.capacity());

  const auto evictions = makeSpace(size, destructorCb);
  auto alloc = storage_.allocate(size);
  XDCHECK(!alloc.done());
  RegionBucketEntry::create(alloc.view(), hk, value);

  return evictions;
}

void RegionBucket::insert(RegionBucketStorage::Allocation alloc,
                        HashedKey hk,
                        BufferView value) {
  XDCHECK(!alloc.done());
  RegionBucketEntry::create(alloc.view(), hk, value);
}

uint32_t RegionBucket::computeKVSize(HashedKey hk,BufferView value) {
  const auto size = RegionBucketEntry::computeSize(hk.key().size(), value.size());
  return size;
}

RegionBucketStorage::Allocation RegionBucket::allocate(HashedKey hk,
    BufferView value) {
  const auto size = RegionBucketEntry::computeSize(hk.key().size(), value.size());
  XDCHECK_LE(size, storage_.remainingCapacity());

  auto alloc = storage_.allocate(size);
  XDCHECK(!alloc.done());
  return alloc;
}

uint32_t RegionBucket::remainCapacity() {
  return storage_.remainingCapacity();
}

void RegionBucket::clear() {
  storage_.clear();
}

uint32_t RegionBucket::slotSize() {
  const auto size = RegionBucketEntry::computeSize(0, 0);
  const auto requiredSize = RegionBucketStorage::slotSize(size);
  return requiredSize;
}

bool RegionBucket::isSpace(HashedKey hk, BufferView value) {
  const auto size = RegionBucketEntry::computeSize(hk.key().size(), value.size());
  const auto requiredSize = RegionBucketStorage::slotSize(size);
  XDCHECK_LE(requiredSize, storage_.capacity());

  auto curFreeSpace = storage_.remainingCapacity();
  return (curFreeSpace >= requiredSize);
}

bool RegionBucket::ifNotReachBucketThreshold(HashedKey hk, BufferView value, int16_t flushThreshold) { 
  int64_t size = RegionBucketEntry::computeSize(hk.key().size(), value.size());
  int64_t requiredSize = RegionBucketStorage::slotSize(size);
  // XLOG(INFO) << "requiredSize: " << requiredSize; 
  XDCHECK_LE(requiredSize, storage_.capacity());
  int64_t curFreeSpace = storage_.remainingCapacity();
  if ((100 * (curFreeSpace - requiredSize)) >= (storage_.capacity() * (100 - flushThreshold)))   
      return true;
  return false;
}

uint32_t RegionBucket::makeSpace(uint32_t size,
                           const DestructorCallback& destructorCb) {
  const auto requiredSize = RegionBucketStorage::slotSize(size);
  XDCHECK_LE(requiredSize, storage_.capacity());

  auto curFreeSpace = storage_.remainingCapacity();
  if (curFreeSpace >= requiredSize) {
    return 0;
  }

  uint32_t evictions = 0;
  auto itr = storage_.getFirst();
  while (true) {
    evictions++;

    if (destructorCb) {
      auto* entry = getIteratorEntry(itr);
      destructorCb(entry->hashedKey(), entry->value(), DestructorEvent::Recycled);
    }

    curFreeSpace += RegionBucketStorage::slotSize(itr.view().size());
    if (curFreeSpace >= requiredSize) {
      storage_.removeUntil(itr);
      break;
    }
    itr = storage_.getNext(itr);
    XDCHECK(!itr.done());
  }
  return evictions;
}

uint32_t RegionBucket::remove(HashedKey hk, const DestructorCallback& destructorCb) {
  auto itr = storage_.getFirst();
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    if (entry->keyEqualsTo(hk)) {
      if (destructorCb) {
        destructorCb(entry->hashedKey(), entry->value(), DestructorEvent::Removed);
      }
      storage_.remove(itr);
      return 1;
    }
    itr = storage_.getNext(itr);
  }
  return 0;
}

void RegionBucket::reorder(BitVectorReadVisitor isHitCallback) {
  uint32_t keyIdx = 0;
  auto itr = storage_.getFirst();
  while (!itr.done()) {
    auto* entry = getIteratorEntry(itr);
    bool hit = isHitCallback(keyIdx);
    if (hit) {
      auto key = Buffer(entry->key());
      auto value = Buffer(entry->value());
      HashedKey hk = makeHK(key.view());
      BufferView valueView = value.view();
      storage_.remove(itr);
      const auto size = RegionBucketEntry::computeSize(hk.key().size(), valueView.size());
      auto alloc = storage_.allocate(size);
      RegionBucketEntry::create(alloc.view(), hk, valueView);
    }

    keyIdx++;
    itr = storage_.getNext(itr);
  }
}

RegionBucket::Iterator RegionBucket::getFirst() const {
  return Iterator{storage_.getFirst()};
}

RegionBucket::Iterator RegionBucket::getNext(Iterator itr) const {
  return Iterator{storage_.getNext(itr.itr_)};
}
} // namespace navy
} // namespace cachelib
} // namespace facebook