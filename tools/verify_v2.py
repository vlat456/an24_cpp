import json

with open("blueprint_v2_new.json") as f:
    j = json.load(f)

print("=== Sub-blueprints ===")
for k, sb in j.get("sub_blueprints", {}).items():
    print(f"  {k}:")
    print(f"    template: {sb.get('template')}")
    print(f"    type_name: {sb.get('type_name')}")
    is_embedded = "nodes" in sb
    print(f"    embedded: {is_embedded}")
    if is_embedded:
        print(f"    nodes: {list(sb['nodes'].keys())}")
        print(f"    wires: {len(sb.get('wires', []))}")
    if "overrides" in sb:
        ov = sb["overrides"]
        print(f"    overrides.layout keys: {list(ov.get('layout', {}).keys())}")

print()
print("=== Wire sample (first 5) ===")
for w in j["wires"][:5]:
    print(f"  {w['id']}: {w['from']} -> {w['to']}")

print()
print("=== Stats ===")
print(f"Nodes: {len(j['nodes'])}")
print(f"Top-level wires: {len(j['wires'])}")
print(f"Sub-blueprints: {len(j.get('sub_blueprints', {}))}")

# Check no prefixed layout keys
for k, sb in j.get("sub_blueprints", {}).items():
    if "overrides" in sb and "layout" in sb["overrides"]:
        for lk in sb["overrides"]["layout"]:
            if ":" in lk:
                print(f"  BUG: prefixed layout key '{lk}' in {k}")
