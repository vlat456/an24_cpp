#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
from typing import Any, Dict, List, Tuple


def to_v3_path(node: str, port: str) -> str:
    return f"/{node}:{port}"


def convert_ports_from_exposes(exposes: Dict[str, Any]) -> List[Dict[str, Any]]:
    out: List[Dict[str, Any]] = []
    dir_map = {"In": 0, "Out": 1, "InOut": 2}
    domain_map = {
        "V": 1,
        "I": 1,
        "Bool": 2,
        "RPM": 4,
        "Pressure": 8,
        "Temperature": 16,
        "Position": 4,
        "Any": 1,
    }
    for name, meta in exposes.items():
        direction = dir_map.get(str(meta.get("direction", "Out")), 1)
        ptype = str(meta.get("type", "Any"))
        domain = domain_map.get(ptype, 1)
        out.append(
            {
                "name": name,
                "domain": domain,
                "direction": direction,
                "type": ptype,
            }
        )
    return out


def convert_node(node_id: str, n: Dict[str, Any]) -> Dict[str, Any]:
    out: Dict[str, Any] = {
        "id": node_id,
        "type": n.get("type", ""),
    }
    if "display_name" in n:
        out["name"] = n["display_name"]
    if "render_hint" in n:
        out["render_hint"] = n["render_hint"]
    if "expandable" in n:
        out["expandable"] = bool(n["expandable"])
    if "group_id" in n:
        out["group_id"] = n["group_id"]
    if "blueprint_path" in n:
        out["blueprint_path"] = n["blueprint_path"]

    pos = n.get("pos")
    if isinstance(pos, list) and len(pos) >= 2:
        out["position"] = {"x": float(pos[0]), "y": float(pos[1])}

    size = n.get("size")
    if isinstance(size, list) and len(size) >= 2:
        out["width"] = float(size[0])
        out["height"] = float(size[1])

    params = n.get("params")
    if isinstance(params, dict):
        fparams: Dict[str, float] = {}
        for k, v in params.items():
            try:
                fparams[k] = float(v)
            except Exception:
                continue
        if fparams:
            out["params"] = fparams

    if isinstance(n.get("content"), dict):
        c = n["content"]
        kind_to_content = {
            "gauge": 1,
            "switch": 2,
            "vertical_toggle": 3,
            "slider": 6,
            "text": 5,
        }
        out["content_type"] = kind_to_content.get(str(c.get("kind", "")).lower(), 0)
        out["content_label"] = str(c.get("label", ""))
        out["content_value"] = float(c.get("value", 0.0))
        out["content_min"] = float(c.get("min", 0.0))
        out["content_max"] = float(c.get("max", 1.0))
        out["content_unit"] = str(c.get("unit", ""))

    if isinstance(n.get("color"), dict):
        col = n["color"]
        out["has_color"] = True
        out["color_r"] = float(col.get("r", 0.5))
        out["color_g"] = float(col.get("g", 0.5))
        out["color_b"] = float(col.get("b", 0.5))
        out["color_a"] = float(col.get("a", 1.0))

    if isinstance(n.get("layout_overrides"), list):
        out_overrides = []
        for ov in n["layout_overrides"]:
            if not isinstance(ov, dict):
                continue
            new_ov = {"port_name": str(ov.get("port", ""))}
            if "side" in ov:
                new_ov["side"] = str(ov["side"])
            if "position" in ov:
                try:
                    new_ov["position"] = int(ov["position"])
                except Exception:
                    pass
            out_overrides.append(new_ov)
        if out_overrides:
            out["layout_overrides"] = out_overrides

    return out


def convert_wire(w: Dict[str, Any]) -> Dict[str, Any]:
    src = w.get("from", ["", ""])
    tgt = w.get("to", ["", ""])
    out = {
        "id": str(w.get("id", "")),
        "source": to_v3_path(str(src[0]), str(src[1])) if len(src) >= 2 else "",
        "target": to_v3_path(str(tgt[0]), str(tgt[1])) if len(tgt) >= 2 else "",
    }
    if isinstance(w.get("routing"), list):
        pts = []
        for p in w["routing"]:
            if isinstance(p, list) and len(p) >= 2:
                pts.append([float(p[0]), float(p[1])])
        if pts:
            out["routing_points"] = pts
    return out


def convert_subblueprint(sb_id: str, sb: Dict[str, Any]) -> Dict[str, Any]:
    out: Dict[str, Any] = {
        "id": sb_id,
        "embedded": True,
    }
    pos = sb.get("pos")
    if isinstance(pos, list) and len(pos) >= 2:
        out["position"] = {"x": float(pos[0]), "y": float(pos[1])}
    if "type_name" in sb:
        out["blueprint"] = sb.get("type_name", "")

    inner_nodes = []
    for nid, node in sb.get("nodes", {}).items():
        if isinstance(node, dict):
            inner_nodes.append(convert_node(nid, node))
    inner_wires = []
    for w in sb.get("wires", []):
        if isinstance(w, dict):
            inner_wires.append(convert_wire(w))

    inner_def: Dict[str, Any] = {
        "version": "3.0",
        "id": sb.get("type_name", sb_id),
        "display_name": sb.get("template", sb.get("type_name", sb_id)),
        "interface": [],
        "nodes": inner_nodes,
        "wires": inner_wires,
        "nested": [],
    }
    out["definition"] = inner_def
    return out


def convert_document(src: Dict[str, Any], file_stem: str) -> Dict[str, Any]:
    meta = src.get("meta", {}) if isinstance(src.get("meta"), dict) else {}
    v3: Dict[str, Any] = {
        "version": "3.0",
        "id": meta.get("name", file_stem),
        "display_name": meta.get("name", file_stem),
        "interface": convert_ports_from_exposes(src.get("exposes", {})),
        "nodes": [],
        "wires": [],
        "nested": [],
    }

    # Preserve component/type-definition metadata for v3-only type loading.
    if "cpp_class" in meta:
        v3["cpp_class"] = bool(meta["cpp_class"])
    if "description" in meta:
        v3["description"] = str(meta["description"])
    if isinstance(meta.get("domains"), list):
        v3["domains"] = [str(d) for d in meta["domains"]]
    if "priority" in meta:
        v3["priority"] = str(meta["priority"])
    if "critical" in meta:
        v3["critical"] = bool(meta["critical"])
    if "content_type" in meta:
        v3["content_type"] = str(meta["content_type"])
    if "render_hint" in meta:
        v3["render_hint"] = str(meta["render_hint"])
    if "visual_only" in meta:
        v3["visual_only"] = bool(meta["visual_only"])

    if isinstance(src.get("params"), dict):
        param_defaults: Dict[str, str] = {}
        for k, v in src["params"].items():
            if isinstance(v, dict) and "default" in v:
                param_defaults[str(k)] = str(v["default"])
            elif isinstance(v, str):
                param_defaults[str(k)] = v
        if param_defaults:
            v3["param_defaults"] = param_defaults

    # Default classification policy:
    # - Explicit visual_only => non-cpp_class
    # - Presence of graph content (nodes/wires/sub_blueprints) => composite (cpp_class=false)
    # - Otherwise primitive C++ component (cpp_class=true)
    has_graph = bool(src.get("nodes") or src.get("wires") or src.get("sub_blueprints"))
    if "cpp_class" not in v3:
        if bool(v3.get("visual_only", False)):
            v3["cpp_class"] = False
        elif has_graph:
            v3["cpp_class"] = False
        else:
            v3["cpp_class"] = True

    for nid, n in src.get("nodes", {}).items():
        if isinstance(n, dict):
            v3["nodes"].append(convert_node(nid, n))
    for w in src.get("wires", []):
        if isinstance(w, dict):
            cw = convert_wire(w)
            if cw["source"] and cw["target"]:
                v3["wires"].append(cw)

    sub = src.get("sub_blueprints", {})
    if isinstance(sub, dict):
        for sb_id, sb in sub.items():
            if isinstance(sb, dict):
                v3["nested"].append(convert_subblueprint(sb_id, sb))

    if isinstance(src.get("viewport"), dict):
        vp = src["viewport"]
        pan = vp.get("pan", [0.0, 0.0])
        if isinstance(pan, list) and len(pan) >= 2:
            v3["pan_x"] = float(pan[0])
            v3["pan_y"] = float(pan[1])
        v3["zoom"] = float(vp.get("zoom", 1.0))
        v3["grid_step"] = float(vp.get("grid", 16.0))

    return v3


def convert_file(path: Path) -> Tuple[bool, str]:
    try:
        src = json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:
        return False, f"invalid json: {e}"

    if src.get("version") == "3.0":
        return False, "already v3"

    v3 = convert_document(src, path.stem)
    path.write_text(
        json.dumps(v3, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return True, "converted"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Convert .blueprint v2(flat) to v3(bp2 codec schema)"
    )
    ap.add_argument("roots", nargs="+", help="Files or directories")
    args = ap.parse_args()

    files: List[Path] = []
    for r in args.roots:
        p = Path(r)
        if p.is_file() and p.suffix == ".blueprint":
            files.append(p)
        elif p.is_dir():
            files.extend(sorted(p.rglob("*.blueprint")))

    changed = 0
    for f in files:
        ok, msg = convert_file(f)
        print(f"{f}: {msg}")
        if ok:
            changed += 1

    print(f"converted {changed} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
