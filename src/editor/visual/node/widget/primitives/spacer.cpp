#include "visual/node/widget/primitives/spacer.h"

Spacer::Spacer() {
    flexible_ = true;
}

Pt Spacer::getPreferredSize(IDrawList*) const {
    return Pt(0, 0);
}

void Spacer::render(IDrawList*, Pt, float) const {}
