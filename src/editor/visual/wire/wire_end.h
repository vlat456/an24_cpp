#pragma once
#include "visual/widget.h"

namespace visual {

class Wire;

/// WireEnd lives as child of Port.
/// Has non-owning back-pointer to the Wire it belongs to.
/// On destruction, notifies Wire so it can self-remove from Scene.
class WireEnd : public Widget {
public:
    explicit WireEnd(Wire* wire) : wire_(wire) {}
    ~WireEnd() override;

    Wire* wire() const { return wire_; }
    void setWire(Wire* w) { wire_ = w; }
    void clearWire() { wire_ = nullptr; }

private:
    Wire* wire_;
};

} // namespace visual
