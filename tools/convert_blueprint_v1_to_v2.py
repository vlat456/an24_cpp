#!/usr/bin/env python3
"""Convert a v1 blueprint.json to v2 format.

Usage: python3 convert_blueprint_v1_to_v2.py input.json output.json
"""

import json
import sys

CONTENT_TYPE_MAP = {
    0: "none",
    1: "gauge",
    2: "switch",
    3: "vertical_toggle",
    4: "value",
    5: "text",
}


def convert_pos(pos_obj):
    """Convert {"x": 1, "y": 2} → [1, 2]"""
    if isinstance(pos_obj, list):
        return pos_obj
    return [pos_obj.get("x", 0.0), pos_obj.get("y", 0.0)]


def convert_wire_endpoint(s):
    """Convert "node.port" → ["node", "port"].
    Handles node names with colons like "lamp_pass_through_1:vin.port"
    """
    dot = s.rfind(".")
    if dot < 0:
        return [s, ""]
    return [s[:dot], s[dot + 1:]]


def strip_prefix(nid, prefix):
    """Strip 'sbi_id:' prefix if present, else return as-is."""
    if nid.startswith(prefix):
        return nid[len(prefix):]
    return nid


def convert_device_to_node(dev):
    """Convert a v1 device dict to a v2 node dict."""
    node = {"type": dev["classname"]}

    if "pos" in dev:
        node["pos"] = convert_pos(dev["pos"])
    if "size" in dev:
        node["size"] = convert_pos(dev["size"])
    if dev.get("params"):
        node["params"] = dict(dev["params"])
    if dev.get("display_name"):
        node["display_name"] = dev["display_name"]
    if dev.get("render_hint"):
        node["render_hint"] = dev["render_hint"]
    if dev.get("expandable"):
        node["expandable"] = True
    if dev.get("group_id"):
        node["group_id"] = dev["group_id"]
    if dev.get("blueprint_path"):
        node["blueprint_path"] = dev["blueprint_path"]

    if dev.get("color") and isinstance(dev["color"], dict):
        c = dev["color"]
        node["color"] = {"r": c["r"], "g": c["g"], "b": c["b"], "a": c["a"]}

    if dev.get("content") and isinstance(dev["content"], dict):
        ct = dev["content"]
        content = {
            "kind": CONTENT_TYPE_MAP.get(ct.get("type", 0), "none"),
            "label": ct.get("label", ""),
            "value": ct.get("value", 0.0),
            "min": ct.get("min", 0.0),
            "max": ct.get("max", 1.0),
            "unit": ct.get("unit", ""),
        }
        if "state" in ct:
            content["state"] = ct["state"]
        node["content"] = content

    return node


def convert_v1_to_v2(v1):
    v2 = {
        "version": 2,
        "meta": {"name": ""},
    }

    # ── Viewport ──
    vp = v1.get("viewport", {})
    v2["viewport"] = {
        "pan": convert_pos(vp.get("pan", {"x": 0, "y": 0})),
        "zoom": vp.get("zoom", 1.0),
        "grid": vp.get("grid_step", 16.0),
    }

    # ── Build SBI lookup ──
    sbis = v1.get("sub_blueprint_instances", [])
    sbi_by_id = {}
    baked_in_internal_ids = set()
    non_baked_in_internal_ids = set()
    for sbi in sbis:
        sbi_id = sbi["id"]
        sbi_by_id[sbi_id] = sbi
        for nid in sbi.get("internal_node_ids", []):
            if sbi.get("baked_in", False):
                baked_in_internal_ids.add(nid)
            else:
                non_baked_in_internal_ids.add(nid)

    # ── Devices → nodes (top-level) ──
    device_by_name = {}
    nodes = {}
    for dev in v1.get("devices", []):
        name = dev["name"]
        device_by_name[name] = dev

        # Skip internal nodes of NON-baked-in SBIs (re-expanded from registry)
        if name in non_baked_in_internal_ids:
            continue

        nodes[name] = convert_device_to_node(dev)

    v2["nodes"] = nodes

    # ── Wires ──
    # Identify internal wires of baked-in SBIs (for dual storage)
    baked_in_wires = {sbi["id"]: [] for sbi in sbis if sbi.get("baked_in", False)}
    wires_v2 = []
    wire_id_counter = 0

    for wire in v1.get("wires", []):
        from_ep = convert_wire_endpoint(wire["from"])
        to_ep = convert_wire_endpoint(wire["to"])
        from_node = from_ep[0]
        to_node = to_ep[0]

        routing = [convert_pos(rp) for rp in wire.get("routing_points", [])]

        wv = {
            "id": f"w_{wire_id_counter}",
            "from": from_ep,
            "to": to_ep,
        }
        if routing:
            wv["routing"] = routing
        wire_id_counter += 1

        # Check if both endpoints belong to a baked-in SBI
        assigned_sbi = None
        for sbi_id, sbi in sbi_by_id.items():
            if sbi.get("baked_in", False):
                internal_ids = set(sbi.get("internal_node_ids", []))
                if from_node in internal_ids and to_node in internal_ids:
                    assigned_sbi = sbi_id
                    break

        if assigned_sbi:
            baked_in_wires[assigned_sbi].append(wv)

        # Skip wires where BOTH endpoints are non-baked-in internals
        if from_node in non_baked_in_internal_ids and to_node in non_baked_in_internal_ids:
            continue
        # Skip wires where either endpoint is a non-baked-in internal (not emitted)
        if from_node in non_baked_in_internal_ids or to_node in non_baked_in_internal_ids:
            continue

        wires_v2.append(wv)

    v2["wires"] = wires_v2

    # ── Sub-blueprints ──
    sub_blueprints = {}
    for sbi in sbis:
        sbi_id = sbi["id"]
        prefix = sbi_id + ":"

        sb = {}
        if sbi.get("blueprint_path"):
            sb["template"] = sbi["blueprint_path"]
        if sbi.get("type_name"):
            sb["type_name"] = sbi["type_name"]
        sb["pos"] = convert_pos(sbi.get("pos", {"x": 0, "y": 0}))
        sb["size"] = convert_pos(sbi.get("size", {"x": 0, "y": 0}))
        sb["collapsed"] = True

        if sbi.get("baked_in", False):
            # ── Embedded mode: store internal nodes/wires inline ──
            embedded_nodes = {}
            for nid in sbi.get("internal_node_ids", []):
                dev = device_by_name.get(nid)
                if dev is None:
                    continue
                local_id = strip_prefix(nid, prefix)
                embedded_nodes[local_id] = convert_device_to_node(dev)

            if embedded_nodes:
                sb["nodes"] = embedded_nodes

            # Internal wires with unprefixed endpoints
            internal_wires = []
            for wv in baked_in_wires.get(sbi_id, []):
                unprefixed_wv = dict(wv)
                unprefixed_wv["from"] = [strip_prefix(wv["from"][0], prefix), wv["from"][1]]
                unprefixed_wv["to"] = [strip_prefix(wv["to"][0], prefix), wv["to"][1]]
                internal_wires.append(unprefixed_wv)
            if internal_wires:
                sb["wires"] = internal_wires
        else:
            # ── Reference mode: convert overrides ──
            overrides = {}

            layout_ov = sbi.get("layout_override", {})
            if layout_ov:
                layout = {}
                for k, v in layout_ov.items():
                    local_key = strip_prefix(k, prefix)
                    layout[local_key] = convert_pos(v)
                overrides["layout"] = layout

            params_ov = sbi.get("params_override", {})
            if params_ov:
                overrides["params"] = params_ov

            routing_ov = sbi.get("internal_routing", {})
            if routing_ov:
                routing = {}
                for k, pts in routing_ov.items():
                    routing[k] = [convert_pos(p) for p in pts]
                overrides["routing"] = routing

            if overrides:
                sb["overrides"] = overrides

        sub_blueprints[sbi_id] = sb

    if sub_blueprints:
        v2["sub_blueprints"] = sub_blueprints

    return v2


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} input.json output.json")
        sys.exit(1)

    with open(sys.argv[1], "r") as f:
        v1 = json.load(f)

    v2 = convert_v1_to_v2(v1)

    with open(sys.argv[2], "w") as f:
        json.dump(v2, f, indent=2)
        f.write("\n")

    n_nodes = len(v2.get("nodes", {}))
    n_wires = len(v2.get("wires", []))
    n_subs = len(v2.get("sub_blueprints", {}))
    print(f"Converted: {n_nodes} nodes, {n_wires} wires, {n_subs} sub_blueprints")


if __name__ == "__main__":
    main()
