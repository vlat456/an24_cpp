#include "visual/wire/wire_end.h"
#include "visual/wire/wire.h"

namespace visual {

WireEnd::~WireEnd() {
    if (wire_) wire_->onEndpointDestroyed(this);
}

void WireEnd::clearWire() {
    if (wire_) {
        wire_->detachEndpoint(this);
        wire_ = nullptr;
    }
}

} // namespace visual
