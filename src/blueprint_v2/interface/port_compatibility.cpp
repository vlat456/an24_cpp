#include "port_compatibility.h"

namespace bp2 {

PortDomainResolution resolve_port_domain(const PortDescriptor& source,
                                         const PortDescriptor& target) {
    const bool src_any = (source.port_type == PortType::Any);
    const bool tgt_any = (target.port_type == PortType::Any);

    if (src_any && tgt_any) {
        return {source.domain, PortDomainResolutionKind::BothAnyAmbiguous};
    }
    if (src_any) {
        return {target.domain, PortDomainResolutionKind::SourceAnyAdoptsTarget};
    }
    if (tgt_any) {
        return {source.domain, PortDomainResolutionKind::TargetAnyAdoptsSource};
    }
    if (source.domain != target.domain) {
        return {std::nullopt, PortDomainResolutionKind::Mismatch};
    }
    return {source.domain, PortDomainResolutionKind::ExactMatch};
}

bool port_domains_compatible(const PortDescriptor& source,
                             const PortDescriptor& target) {
    return resolve_port_domain(source, target).compatible();
}

} // namespace bp2
