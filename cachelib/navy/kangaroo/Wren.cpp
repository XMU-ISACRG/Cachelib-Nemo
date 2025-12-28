#include <mutex>
#include <shared_mutex>

#include "cachelib/navy/kangaroo/Wren.h"

namespace facebook {
namespace cachelib {
namespace navy {

Wren::EuIterator Wren::getEuIterator() {
  XLOGF(INFO, "WREN: getting iterator: write {}, erase {}", writeEraseUnit_, eraseEraseUnit_);
  for (uint64_t i = 0; i < numBuckets_; i++) {
    if (kbidToEuid_[i].euid_.index() / bucketsPerEu_ == eraseEraseUnit_) {
      return EuIterator(KangarooBucketId(i));
    }
  }
  return EuIterator();
}

Wren::EuIterator Wren::getNext(EuIterator euit) {
  for (uint64_t i = euit.getBucket().index() + 1; i < numBuckets_; i++) {
    if (kbidToEuid_[i].euid_.index() / bucketsPerEu_ == eraseEraseUnit_) {
      return EuIterator(KangarooBucketId(i));
    }
  }
  return EuIterator();
}

Wren::Wren(Device& device, uint64_t numBuckets, uint64_t bucketSize, uint64_t totalSize, uint64_t setOffset)
          : device_{device}, 
          euCap_{device_.getIOZoneCapSize()},
          numEus_{totalSize / euCap_},
          eraseEraseUnit_{numEus_ - 1},
          numBuckets_{numBuckets}, 
          bucketSize_{bucketSize},
          setOffset_{setOffset},
          bucketsPerEu_{euCap_ / bucketSize_} {
  kbidToEuid_ = new EuIdentifier[numBuckets_];
  setStat.reserve(numEus_ * bucketsPerEu_);
  validSetsPerzone.reserve(numEus_);
  logToSetValidSetsPerzone.reserve(numEus_);
  setGCValidSetsPerzone.reserve(numEus_);
  for (uint64_t i = 0; i < numEus_ * bucketsPerEu_; ++i)
    setStat[i].set(0);
  for (uint64_t i = 0; i < numEus_; ++i) {
    validSetsPerzone[i].set(0);
    logToSetValidSetsPerzone[i].set(0);
    setGCValidSetsPerzone[i].set(0);
  }
  XLOGF(INFO, "Num WREN zones {} with capacity {} from size {}, starting at writeEraseUnit_ {} til eraseEraseUnit_ {}, bucketsPerEu_ {}. Hot/Cold set loc based on {}(setOffset)", 
      numEus_, euCap_, totalSize, writeEraseUnit_, eraseEraseUnit_, bucketsPerEu_, setOffset_);
  XLOG(INFO) << "WREN total numBuckets = " << numBuckets << ", bucketsPerEu = " << bucketsPerEu_;
}

Wren::~Wren() {
  delete kbidToEuid_;
}

Wren::EuId Wren::calcEuId(uint32_t erase_unit, uint32_t offset) {
  uint64_t euOffset = erase_unit * bucketsPerEu_;
  return EuId(euOffset + offset);
}

Wren::EuId Wren::findEuId(KangarooBucketId kbid) {
  XDCHECK(kbid.index() < numBuckets_);
  return kbidToEuid_[kbid.index()].euid_;
}

uint64_t Wren::getEuIdLoc(uint32_t erase_unit, uint32_t offset) {
  return getEuIdLoc(calcEuId(erase_unit, offset));
}

uint64_t Wren::getEuIdLoc(EuId euid) {
  uint64_t zone_offset = euid.index() % bucketsPerEu_;
  uint64_t zone = euid.index() / bucketsPerEu_;
  uint64_t offset = setOffset_ + zone_offset * bucketSize_ 
    + zone * device_.getIOZoneSize();
  return offset;
}

Buffer Wren::read(KangarooBucketId kbid, bool& newBuffer) {
  EuId euid = findEuId(kbid);
  if (euid.index() >= numEus_ * euCap_) { 
    newBuffer = true;
    return device_.makeIOBuffer(bucketSize_);
  }
  uint64_t loc = getEuIdLoc(euid);

  auto buffer = device_.makeIOBuffer(bucketSize_);
  XDCHECK(!buffer.isNull());
  newBuffer = false;

  const bool res = device_.read(loc, buffer.size(), buffer.data());
  readSSDCount_.inc();
  if (!res) {
    return {};
  }

  return buffer;
}

bool Wren::write(KangarooBucketId kbid, Buffer buffer) {
  {
    std::unique_lock<folly::SharedMutex> lock{writeMutex_};
    if (writeEraseUnit_ == eraseEraseUnit_) {
        return false;
    }
  
    if (writeOffset_ == 0) {
      XLOGF(INFO, "WREN Write: reseting zone {}, {} / {}", 
          getEuIdLoc(writeEraseUnit_, 0)/device_.getIOZoneSize(),
          writeEraseUnit_, numEus_);
      device_.reset(getEuIdLoc(writeEraseUnit_, 0), device_.getIOZoneSize());
    }

    EuId euid = calcEuId(writeEraseUnit_, writeOffset_);
    uint64_t loc = getEuIdLoc(euid);
    XDCHECK(euid.index() < numEus_ * euCap_);

    bool ret = device_.write(loc, std::move(buffer));
    if (!ret) {
      XLOGF(INFO, "tried to write at {} euid, {}.{} calculated zone + offset, write zone {}, loc {}", 
          euid.index(), euid.index() / (euCap_/ bucketSize_), euid.index() % (euCap_/bucketSize_),
          writeEraseUnit_, loc);
      kbidToEuid_[kbid.index()].euid_ = EuId(-1); 
      writeOffset_ = euCap_/bucketSize_; 
    } else {
      kbidToEuid_[kbid.index()].euid_ = euid;
      writeOffset_++;
    }
    
    if (writeOffset_ >= euCap_/bucketSize_) {
      device_.finish(getEuIdLoc(writeEraseUnit_, 0), device_.getIOZoneSize());
      XLOGF(INFO, "WREN Write: finishing zone {} old eu {} / {}, euid {}", 
          getEuIdLoc(writeEraseUnit_, 0)/device_.getIOZoneSize(), writeEraseUnit_, 
          numEus_, calcEuId(writeEraseUnit_, 0).index());
      writeEraseUnit_ = (writeEraseUnit_ + 1) % numEus_;

      writeOffset_ = 0;
    }

    return ret;
  }
}

uint64_t Wren::getPybid(KangarooBucketId kbid) {
  return kbidToEuid_[kbid.index()].euid_.index();
}

uint64_t Wren::getPyzone(KangarooBucketId kbid) {
  return kbidToEuid_[kbid.index()].euid_.index() / bucketsPerEu_;
}

void Wren::invalidMappingTable(KangarooBucketId kbid) {
  kbidToEuid_[kbid.index()].euid_ = EuId(-1); // fail bucket write
}

void Wren::addValid(uint64_t newPybid, uint64_t newPyzone, bool logFlush) {
  validSetsPerzone[newPyzone].inc();
  if (logFlush) {
      setStat[newPybid].set(1);  
      logToSetValidSetsPerzone[newPyzone].inc();
  } else {
      setStat[newPybid].set(2);  
      setGCValidSetsPerzone[newPyzone].inc();
  }
}

void Wren::subValid(uint64_t oldPybid, uint64_t oldPyzone, bool logFlush) {
  validSetsPerzone[oldPyzone].dec();

  if (setStat[oldPybid].get() == 1) {
      logToSetValidSetsPerzone[oldPyzone].dec();
  } else if (setStat[oldPybid].get() == 2) {
      setGCValidSetsPerzone[oldPyzone].dec();
  }
  setStat[oldPybid].set(-1);

  if (logFlush) logGCInValidSets.inc();
  else setGCInValidSets.inc();
}

double Wren::setGCinvalidSetRatio() {
  if (setGCInValidSets.get() + logGCInValidSets.get() != 0) {
    return 100.0 * setGCInValidSets.get() / (setGCInValidSets.get() + logGCInValidSets.get());
  } 
  return 0;
}

uint64_t Wren::getSetGCinvalidSets() {
  return setGCInValidSets.get();
}

uint64_t Wren::getLogGCinvalidSets() {
  return logGCInValidSets.get();
}

bool Wren::shouldClean(double cleaningThreshold) {
  uint32_t freeEus = 0;
  uint32_t writeEu = writeEraseUnit_;
  if (eraseEraseUnit_ >= writeEu) {
    freeEus = eraseEraseUnit_ - writeEu;
  } else {
    freeEus = eraseEraseUnit_ + (numEus_ - writeEu);
  }

  return freeEus <= cleaningThreshold * numEus_;
}

bool Wren::erase() {
  EuId euid = calcEuId(eraseEraseUnit_, 0);
  eraseEraseUnit_ = (eraseEraseUnit_ + 1) % numEus_;

  return device_.reset(euid.index(), euCap_);
}

} // namespace navy
} // namespace cachelib
} // namespace facebook
