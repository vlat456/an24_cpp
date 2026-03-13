#pragma once
#include "visual/widget.h"
#include "visual/port/visual_port.h"

namespace visual {

class Wire;

/// WireEnd lives as child of Port.
/// Has non-owning back-pointer to the Wire it belongs to.
/// On destruction, notifies Wire so it can self-remove from Scene.
/// Offset to port center so wire endpoints connect to the circle center.
class WireEnd : public Widget {
public:
    explicit WireEnd(Wire* wire) : wire_(wire) {
        setLocalPos(Pt(Port::RADIUS, Port::RADIUS));
    }
    ~WireEnd() override;

    Wire* wire() const { return wire_; }
    void setWire(Wire* w) { wire_ = w; }
    
    /// Break the connection to the Wire in both directions.
    /// Clears this->wire_ and also calls wire->detachEndpoint(this).
    /// Safe to call multiple times (idempotent).
    void clearWire();

private:
    Wire* wire_;
};

} // namespace visual
