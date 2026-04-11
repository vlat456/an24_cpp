#!/usr/bin/env python3

import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
LIBRARY_INDEX_PATH = ROOT / "library" / "library_index.json"


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def dump_json(path: Path, data):
    with path.open("w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2)
        fh.write("\n")


def decode_direction(value):
    if value == 0 or value == "In":
        return "In"
    if value == 1 or value == "Out":
        return "Out"
    if value == 2 or value == "InOut":
        return "InOut"
    raise ValueError(f"unsupported direction: {value!r}")


def routing_points(wire):
    routing = []
    for point in wire.get("routing_points", []):
        if isinstance(point, list) and len(point) == 2:
            routing.append([point[0], point[1]])
            continue
        if isinstance(point, dict) and "x" in point and "y" in point:
            routing.append([point["x"], point["y"]])
            continue
        raise ValueError(f"unsupported routing point: {point!r}")
    return routing


def parse_legacy_endpoint(endpoint: str):
    if not endpoint.startswith("/"):
        raise ValueError(f"expected absolute legacy endpoint, got {endpoint!r}")
    payload = endpoint[1:]
    parts = payload.split(":")
    if len(parts) == 2:
        return parts[0], parts[1]
    if len(parts) >= 3:
        return ":".join(parts[:-1]), parts[-1]
    raise ValueError(f"malformed legacy endpoint: {endpoint!r}")


def sorted_by_id(items):
    return sorted(items, key=lambda item: item["id"])


def make_layout(node):
    layout = {
        "x": node["position"]["x"],
        "y": node["position"]["y"],
    }
    if "width" in node:
        layout["width"] = node["width"]
    if "height" in node:
        layout["height"] = node["height"]
    return layout


def merge_params(node):
    params = {}
    for key, value in node.get("params", {}).items():
        params[key] = value
    for key, value in node.get("string_params", {}).items():
        params[key] = value
    return params


def convert_interface(legacy_bp):
    if "interface" in legacy_bp:
        result = []
        for port in legacy_bp["interface"]:
            entry = {
                "id": port["name"],
                "direction": decode_direction(port["direction"]),
                "port_type": port["type"],
            }
            if port.get("source_writer"):
                entry["source_writer"] = True
            result.append(entry)
        return sorted(result, key=lambda port: port["id"])

    derived = []
    for node in legacy_bp.get("nodes", []):
        if node.get("type") not in ("BlueprintInput", "BlueprintOutput"):
            continue
        params = merge_params(node)
        direction = params.get("exposed_direction")
        if not direction:
            direction = "In" if node["type"] == "BlueprintInput" else "Out"
        port_type = params.get("exposed_type", "V")
        entry = {
            "id": node["name"],
            "direction": direction,
            "port_type": port_type,
        }
        derived.append(entry)
    return sorted(derived, key=lambda port: port["id"])


def convert_component_node(node):
    converted = {
        "id": node["id"],
        "kind": "component",
    }
    label = node.get("name", "")
    if label:
        converted["label"] = label
    converted["component"] = node["type"]
    params = merge_params(node)
    if params:
        converted["params"] = params
    converted["layout"] = make_layout(node)
    return converted


def convert_wire(wire, endpoint_mapper=None):
    src_node, src_port = parse_legacy_endpoint(wire["source"])
    dst_node, dst_port = parse_legacy_endpoint(wire["target"])
    if endpoint_mapper is not None:
        src = endpoint_mapper(src_node, src_port)
        dst = endpoint_mapper(dst_node, dst_port)
        if src is None or dst is None:
            return None
        src_node, src_port = src
        dst_node, dst_port = dst

    converted = {
        "id": wire["id"],
        "from": {"node": src_node, "port": src_port},
        "to": {"node": dst_node, "port": dst_port},
    }
    routing = routing_points(wire)
    if routing:
        converted["routing"] = routing
    return converted


def apply_first_order_lag_instance_params(blueprint, instance_params):
    if not instance_params:
        return blueprint

    for node in blueprint["nodes"]:
        if node["id"] != "accumulator":
            continue
        if "initial_val" not in instance_params:
            continue
        params = node.setdefault("params", {})
        params["initial_val"] = instance_params["initial_val"]
    return blueprint


def convert_legacy_blueprint(legacy_bp):
    if legacy_bp.get("nested"):
        raise ValueError(
            f"nested legacy definitions inside '{legacy_bp.get('id', '<unnamed>')}' are unsupported"
        )

    blueprint_id = (
        legacy_bp.get("id") or legacy_bp.get("name") or legacy_bp.get("display_name")
    )
    blueprint_name = (
        legacy_bp.get("name") or legacy_bp.get("display_name") or legacy_bp.get("id")
    )
    if not blueprint_id or not blueprint_name:
        raise ValueError(f"legacy blueprint missing id/name: {legacy_bp!r}")

    converted = {
        "format": "blueprint",
        "version": 1,
        "blueprint_id": blueprint_id,
        "name": blueprint_name,
        "interface": convert_interface(legacy_bp),
        "nodes": [],
        "wires": [],
    }

    for node in legacy_bp.get("nodes", []):
        converted["nodes"].append(convert_component_node(node))
    converted["nodes"] = sorted_by_id(converted["nodes"])

    for wire in legacy_bp.get("wires", []):
        converted["wires"].append(convert_wire(wire))
    converted["wires"] = sorted_by_id(converted["wires"])
    return converted


def build_library_index():
    index = load_json(LIBRARY_INDEX_PATH)
    entries = {}
    for entry in index["entries"]:
        entries[entry["blueprint_id"]] = ROOT / entry["path"]
    return entries


def convert_root_blueprint(legacy_root, library_index):
    nested_defs = {
        item["id"]: convert_legacy_blueprint(item["definition"])
        for item in legacy_root.get("nested", [])
    }

    host_nodes = [node for node in legacy_root["nodes"] if "group_id" not in node]
    child_nodes = {
        node["id"]: node for node in legacy_root["nodes"] if "group_id" in node
    }

    def map_root_endpoint(node_id, port_id):
        if node_id in child_nodes:
            child = child_nodes[node_id]
            if child.get("type") not in ("BlueprintInput", "BlueprintOutput"):
                return None
            if ":" not in node_id:
                raise ValueError(f"expected grouped bridge node id, got {node_id!r}")
            host_id, exposed_port = node_id.split(":", 1)
            if port_id == "port":
                return host_id, exposed_port
            return None
        return node_id, port_id

    converted = {
        "format": "blueprint",
        "version": 1,
        "blueprint_id": legacy_root.get("name") or "closed_circuit",
        "name": legacy_root["name"],
        "interface": convert_interface(legacy_root),
        "nodes": [],
        "wires": [],
    }

    for node in host_nodes:
        blueprint_path = node.get("blueprint_path")
        if blueprint_path:
            child_blueprint = nested_defs.get(node["id"])
            if child_blueprint is None:
                library_path = library_index.get(node["type"])
                if library_path is None:
                    raise ValueError(
                        f"no embedded definition and no library index entry for '{node['type']}'"
                    )
                child_blueprint = convert_legacy_blueprint(load_json(library_path))
                if node["type"] == "FirstOrderLag":
                    child_blueprint = apply_first_order_lag_instance_params(
                        child_blueprint,
                        node.get("params", {}),
                    )

            converted_node = {
                "id": node["id"],
                "kind": "blueprint_instance",
            }
            label = node.get("name", "")
            if label:
                converted_node["label"] = label
            converted_node["source"] = {
                "mode": "embedded",
                "blueprint": child_blueprint,
            }
            converted_node["layout"] = make_layout(node)
            converted["nodes"].append(converted_node)
            continue

        converted["nodes"].append(convert_component_node(node))

    for wire in legacy_root.get("wires", []):
        converted_wire = convert_wire(wire, endpoint_mapper=map_root_endpoint)
        if converted_wire is None:
            continue
        if converted_wire["from"] == converted_wire["to"]:
            raise ValueError(f"self-wire after migration: {wire['id']}")
        converted["wires"].append(converted_wire)

    converted["nodes"] = sorted_by_id(converted["nodes"])
    converted["wires"] = sorted_by_id(converted["wires"])
    return converted


def main(argv):
    if len(argv) != 2:
        raise SystemExit(f"usage: {argv[0]} <closed_circuit.blueprint>")

    target = Path(argv[1]).resolve()
    legacy_root = load_json(target)
    library_index = build_library_index()
    converted = convert_root_blueprint(legacy_root, library_index)
    dump_json(target, converted)


if __name__ == "__main__":
    main(sys.argv)
