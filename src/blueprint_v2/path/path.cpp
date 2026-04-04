#include "path.h"
#include <string>
#include <optional>

namespace bp2 {

std::string PathArena::to_string(Path p) const {
    if (p.kind() == PathKind::Root) return "/";
    
    std::vector<std::string> parts;
    Path current = p;
    while (current.kind() != PathKind::Root) {
        std::string seg;
        if (current.kind() == PathKind::Port) {
            seg = ":" + std::string(interner_.resolve(current.segment()));
        } else {
            seg = "/" + std::string(interner_.resolve(current.segment()));
        }
        parts.push_back(seg);
        current = parent(current);
    }
    
    std::string result;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        result += *it;
    }
    return result;
}

std::optional<Path> PathArena::parse(std::string_view s) {
    if (s.empty() || s[0] != '/') {
        return std::nullopt;
    }
    if (s == "/") return root();
    
    s.remove_prefix(1);
    
    Path current = root();
    while (!s.empty()) {
        size_t slash_pos = s.find('/');
        size_t colon_pos = s.find(':');
        
        std::string_view token;
        if (slash_pos == std::string_view::npos) {
            token = s;
            s = "";
        } else {
            token = s.substr(0, slash_pos);
            s.remove_prefix(slash_pos + 1);
        }
        
        size_t colon_in_token = token.rfind(':');
        if (colon_in_token != std::string_view::npos) {
            std::string_view node_id = token.substr(0, colon_in_token);
            std::string_view port_name = token.substr(colon_in_token + 1);
            current = make_node(current, interner_.intern(node_id));
            current = make_port(current, interner_.intern(port_name));
        } else {
            current = make_node(current, interner_.intern(token));
        }
    }
    
    return current;
}

} // namespace bp2
