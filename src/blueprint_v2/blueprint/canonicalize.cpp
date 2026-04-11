#include "canonicalize.h"

namespace bp2 {

Blueprint clone_metadata(const Blueprint& bp) {
    Blueprint rebuilt;
    rebuilt = rebuilt.with_id(bp.id());
    rebuilt = rebuilt.with_name(bp.name());
    rebuilt = rebuilt.with_interface(bp.iface());
    return rebuilt;
}

} // namespace bp2
