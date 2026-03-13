#include "visual/wire/wire_end.h"
#include "visual/wire/wire.h"

namespace visual {

WireEnd::~WireEnd() {
    if (wire_) wire_->onEndpointDestroyed(this);
}

} // namespace visual
