#include "cachelib/common/Hash.h"
#include "cachelib/navy/common/Buffer.h"

namespace facebook {
namespace cachelib {
namespace navy {

class Object {
public:
    Object(HashedKey hk_, BufferView value_): hk{hk_}, value{value_} {}
    
private:
    HashedKey hk;
    BufferView value;
};

} // namespace navy
} // namespace cachelib
} // namespace facebook