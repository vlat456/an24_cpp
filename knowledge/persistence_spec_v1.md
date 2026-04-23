# Blueprint Persistence Specification v1

> Canonical source format for blueprint documents.
> This is a strict contract for the new persistence reset.
> No backward compatibility. No fallbacks. No inference. No guessing.

## Scope

This specification defines the new canonical JSON format for:

- blueprint documents
- embedded blueprint instances
- referenced blueprint instances
- interface ports
- node layout
- wires

This specification intentionally does **not** preserve old format concepts.

This specification is grounded by the current project concepts in:

- `src/core/model/component_registry.h`
- `src/io/json/parse_json_api.h`
- `src/blueprint_v2/interface/port_descriptor.h`
- `src/blueprint_v2/blueprint/node_content_type.h`

But it does **not** preserve the old wire format or old split host/nested persistence shape.

## Design Rules

These rules are absolute.

1. A blueprint instance is one persisted object, not two synchronized structures.
2. Embedded blueprint authority is the inline child blueprint object.
3. Referenced blueprint authority is `blueprint_id` only.
4. The blueprint document must not persist derived caches.
5. The blueprint document must not persist editor workspace/session state.
6. The blueprint document must be strict and self-describing.
7. Unknown fields are errors.

## Canonical Document Object

A blueprint document is a single JSON object with exactly these top-level fields.

### Required fields

| Field | Type | Description |
|---|---|---|
| `format` | string | Must be `"blueprint"` |
| `version` | integer | Must be `1` |
| `blueprint_id` | string | Stable opaque identity |
| `name` | string | User-facing blueprint name |
| `interface` | array | Public boundary interface |
| `nodes` | array | Node instances |
| `wires` | array | Local blueprint wires |

### Optional fields

No optional top-level fields are defined in v1.

If additional shared authored document fields are needed later, they must be added explicitly in a future spec revision.

### Forbidden top-level fields

These old-format or non-canonical fields must not appear:

- `display_name`
- `nested`
- `pan_x`
- `pan_y`
- `zoom`
- `grid_step`
- any window/session/editor state

### Minimal valid document

```json
{
  "format": "blueprint",
  "version": 1,
  "blueprint_id": "aircraft.power.dc_main",
  "name": "DC Main",
  "interface": [],
  "nodes": [],
  "wires": []
}
```

## Interface

`interface` is an array of boundary port objects.

### Port object

| Field | Required | Type | Description |
|---|---|---|---|
| `id` | yes | string | Unique port name within this interface |
| `direction` | yes | string | One of `"In"`, `"Out"`, `"InOut"` |
| `port_type` | yes | string | One of `"V"`, `"I"`, `"Signal"`, `"Bool"`, `"RPM"`, `"Temperature"`, `"Pressure"`, `"Position"`, `"Contextual"`, `"Any"` |
| `source_writer` | no | boolean | Defaults to `false` |

### Interface authority

- Blueprint document interface is authoritative for that blueprint.
- `domain` is not persisted in the interface object; it is derived from `port_type`.

### Interface validation

- `id` values must be unique within `interface`
- `direction` must be a recognized token
- `port_type` must be a recognized token
- `source_writer` must be boolean when present

### Example

```json
"interface": [
  { "id": "gnd",   "direction": "In",  "port_type": "V" },
  { "id": "v_in",  "direction": "In",  "port_type": "V" },
  { "id": "v_out", "direction": "Out", "port_type": "V" }
]
```

## Nodes

`nodes` is an array of node objects.

Each node must contain a `kind` field.

### Common node fields

| Field | Required | Type | Description |
|---|---|---|---|
| `id` | yes | string | Stable opaque identity unique within this blueprint |
| `kind` | yes | string | One of `"component"`, `"blueprint_instance"`, `"bridge_port"` |
| `label` | no | string | User-facing label |
| `color` | no | object | Authored per-node RGBA custom color |
| `layout` | yes | object | Layout object |

### Forbidden common node fields

These old-format or non-canonical node fields must not appear:

- `type`
- `ports`
- `expandable`
- `group_id`
- `owner_scope`
- `blueprint_path`
- `render_hint`
- `has_color`
- `content_type`
- `content_label`
- `content_value`
- `content_min`
- `content_max`
- `content_unit`
- `content_state`
- legacy split color fields like `color_r`, `color_g`, `color_b`, `color_a`
- any host interface mirror

### Component node

`kind: "component"`

| Field | Required | Type | Description |
|---|---|---|---|
| `component` | yes | string | Registered component type name |
| `params` | no | object | Parameter values keyed by schema name |

#### Param value types

Param values must match the declared `ParamSchemaType` for that key:

| ParamSchemaType | Allowed JSON type |
|---|---|
| `Float` | number |
| `Int` | number (integer) |
| `Bool` | boolean |
| `String` | string |

No other JSON types are valid as param values.

### Component node authority

- Component interface is derived from the component type registry.
- Component node must not persist interface mirrors.

### Component node validation

- `component` must resolve in the type registry
- `params` keys must exist in that component's parameter schema
- `params` values must match the declared parameter type

Unknown params are errors.

### Canonical node color

If present, `color` must be exactly:

| Field | Required | Type | Description |
|---|---|---|---|
| `r` | yes | number | Red channel, finite authored float |
| `g` | yes | number | Green channel, finite authored float |
| `b` | yes | number | Blue channel, finite authored float |
| `a` | yes | number | Alpha channel, finite authored float |

Node color is canonical authored document state.
It is **not** workspace/session state.

### Blueprint-instance node

`kind: "blueprint_instance"`

| Field | Required | Type | Description |
|---|---|---|---|
| `source` | yes | object | Embedded or referenced blueprint source |
| `collapsed` | no | boolean | Defaults to `true` |

`collapsed` is only valid on `blueprint_instance` nodes. Its presence on a `component` node is an error.

No other blueprint-instance-specific persisted authority exists.

### Blueprint-instance source

`source` must be exactly one of two shapes.

Unknown `mode` values are schema errors.

#### Embedded source

| Field | Required | Type | Description |
|---|---|---|---|
| `mode` | yes | string | Must be `"embedded"` |
| `blueprint` | yes | object | Full child blueprint document |

Example:

```json
"source": {
  "mode": "embedded",
  "blueprint": {
    "format": "blueprint",
    "version": 1,
    "blueprint_id": "embedded.12sam28",
    "name": "12SAM28",
    "interface": [],
    "nodes": [],
    "wires": []
  }
}
```

#### Referenced source

| Field | Required | Type | Description |
|---|---|---|---|
| `mode` | yes | string | Must be `"reference"` |
| `blueprint_id` | yes | string | Referenced blueprint identity |

Example:

```json
"source": {
  "mode": "reference",
  "blueprint_id": "library.power.tru_panel"
}
```

### Blueprint-instance authority

- Embedded instance interface authority = `source.blueprint.interface`
- Referenced instance interface authority = resolved referenced blueprint interface
- Referenced instance identity authority = `source.blueprint_id`

The document must not persist:

- resolved interface
- interface cache
- host-side mirror state
- file path
- nested-sidecar object

### Bridge-port node

`kind: "bridge_port"`

| Field | Required | Type | Description |
|---|---|---|---|
| `exposed_port` | yes | string | Public interface port this bridge anchors |
| `direction` | yes | string | One of `"input"` or `"output"` |
| `port_type` | yes | string | Canonical port type token |

Bridge-port nodes are structural boundary anchors. They are not components.

### Bridge-port authority

- `exposed_port` identifies the authoritative public interface port.
- `direction` and `port_type` are authoritative persisted bridge semantics.
- The bridge's local `ext` / `port` interface is derived from `direction` and `port_type`.

Bridge-port nodes must not persist:

- `component`
- `source`
- `params`
- `collapsed`
- any interface mirror/cache

Canonical blueprint documents must not encode bridge ports as pseudo-components such as `BlueprintInput`, `BlueprintOutput`, or `BridgePort`.

## Layout

Every node has a `layout` object.

### Layout object fields

| Field | Required | Type | Description |
|---|---|---|---|
| `x` | yes | number | World x position |
| `y` | yes | number | World y position |
| `width` | no | number | Explicit width override |
| `height` | no | number | Explicit height override |
| `port_overrides` | no | array | Per-port visual layout overrides |

`collapsed` is a direct `blueprint_instance` node field, not part of `layout`.

### Port override object

| Field | Required | Type | Description |
|---|---|---|---|
| `port_id` | yes | string | Port name |
| `side` | no | string | One of `"left"`, `"right"`, `"top"`, `"bottom"` |
| `position` | no | integer | Explicit port slot index |

### Layout authority

- Node layout is the only persisted instance geometry authority.
- There is no second persisted geometry structure for blueprint instances.

## Wires

`wires` is an array of wire objects local to the containing blueprint.

### Wire object

| Field | Required | Type | Description |
|---|---|---|---|
| `id` | yes | string | Stable opaque identity unique within this blueprint |
| `from` | yes | object | Source endpoint |
| `to` | yes | object | Target endpoint |
| `routing` | no | array | Routing control points |

### Endpoint object

| Field | Required | Type | Description |
|---|---|---|---|
| `node` | yes | string | Node id within this blueprint |
| `port` | yes | string | Port id on that node |

### Routing point

Each entry in `routing` is a two-element numeric array:

```json
[x, y]
```

### Wire validation

- wire ids must be unique
- `from.node` must exist
- `to.node` must exist
- `from.port` must exist on `from.node`
- `to.port` must exist on `to.node`
- `from` and `to` must not be identical

Port existence is resolved from the node's authoritative interface:

- For `component` nodes: the component type registry interface
- For `blueprint_instance` nodes: the source blueprint's `interface` (embedded inline, or resolved referenced blueprint)
- For `bridge_port` nodes: the derived two-port bridge interface from `exposed_port`, `direction`, and `port_type`

## Library Index

Referenced blueprint resolution is outside the blueprint document.

Referenced blueprint instances resolve only through a separate library index.

### Canonical library index format

```json
{
  "format": "library_index",
  "version": 1,
  "entries": [
    {
      "blueprint_id": "library.power.tru_panel",
      "path": "library/power/tru_panel.blueprint.json"
    }
  ]
}
```

### Library index entry

| Field | Required | Type | Description |
|---|---|---|---|
| `blueprint_id` | yes | string | Unique blueprint identity |
| `path` | yes | string | Relative storage path |

### Library rules

- document reference authority is always `blueprint_id`
- path authority exists only in the library index
- canonical blueprint documents must not persist path mirrors
- unknown fields in the library index document or entry objects are schema errors
- `blueprint_id` values must be unique within the library index
- `path` values must be unique within the library index

## Workspace / Session State

Workspace/session/editor state is not part of the blueprint document.

The canonical blueprint document must not contain:

- viewport pan/zoom/grid
- open subwindows
- selection
- inspector state
- per-user editor layout
- runtime probe/simulation values

That data belongs in a separate workspace/session persistence system.

## Canonical Encoding Rules

### Object key order

Top-level blueprint:

1. `format`
2. `version`
3. `blueprint_id`
4. `name`
5. `interface`
6. `nodes`
7. `wires`

Interface port:

1. `id`
2. `direction`
3. `port_type`
4. `source_writer`

Node common:

1. `id`
2. `kind`
3. `label`
4. `component` or `source` (kind-specific)
5. `params` (component only)
6. `collapsed` (blueprint_instance only)
7. `layout`

Layout:

1. `x`
2. `y`
3. `width`
4. `height`
5. `port_overrides`

Blueprint source:

- embedded: `mode`, `blueprint`
- reference: `mode`, `blueprint_id`

Wire:

1. `id`
2. `from`
3. `to`
4. `routing`

Endpoint:

1. `node`
2. `port`

### Array ordering

- `interface` sorted by `id`
- `nodes` sorted by `id`
- `wires` sorted by `id`
- `port_overrides` sorted by `port_id`

### Omission rules

Optional fields are omitted when absent or equal to their default.

Optional arrays (`port_overrides`, `routing`) are omitted when empty.

Do not emit `"port_overrides": []` or `"routing": []`.

### Numeric formatting

- finite only
- minimal decimal form
- no trailing zeros if not needed

## Validation Layers

Validation is strict and layered.

### Layer 1: JSON parse

- input must be valid JSON
- top-level value must be an object

### Layer 2: Schema validation

- all required fields present
- all field types correct
- no unknown fields anywhere
- no forbidden old-format fields anywhere
- kind-specific fields must not appear on wrong kind (e.g. `collapsed` on a `component` node)

### Layer 3: Semantic validation

- `blueprint_id` non-empty, printable ASCII, no whitespace
- `name` non-empty
- node `id` values unique within their containing blueprint
- wire `id` values unique within their containing blueprint
- interface port `id` values unique within `interface`
- component types resolve in the type registry
- component params match declared schema (type and key)
- wire endpoints reference valid nodes and ports (see Wire validation)

### Layer 4: Recursive validation

- embedded child blueprints recursively satisfy this entire spec
- recursive embedded depth must remain bounded by implementation limit

### Layer 5: Reference resolution validation

- every referenced `blueprint_id` required for load/use must resolve through the library index
- unresolved references are errors

There is no offline permissive mode in the canonical path.

## Error Reporting

Errors must identify:

- exact field path
- what was expected
- what was received
- validation layer

Example:

```text
nodes[2].source.mode: expected "embedded" or "reference", got "inline" (schema validation)
```

## Invalid Examples

### Old top-level nested array

```json
{
  "format": "blueprint",
  "version": 1,
  "blueprint_id": "bp",
  "name": "BP",
  "interface": [],
  "nodes": [],
  "wires": [],
  "nested": []
}
```

Invalid: unknown field `nested`.

### Blueprint instance with path authority

```json
{
  "id": "n1",
  "kind": "blueprint_instance",
  "source": {
    "mode": "reference",
    "blueprint_id": "library.power.tru_panel",
    "blueprint_path": "library/power/tru_panel.blueprint.json"
  },
  "layout": { "x": 0, "y": 0 }
}
```

Invalid: path authority is forbidden in blueprint documents.

### Component node with persisted interface mirror

```json
{
  "id": "n1",
  "kind": "component",
  "component": "Battery",
  "ports": {},
  "layout": { "x": 0, "y": 0 }
}
```

Invalid: component interface is derived, not persisted.

### Tagged hierarchy

```json
{
  "id": "n1",
  "kind": "component",
  "component": "Battery",
  "owner_scope": "host1",
  "layout": { "x": 0, "y": 0 }
}
```

Invalid: hierarchy is structural, not tag-based.

### Unknown component param

```json
{
  "id": "n1",
  "kind": "component",
  "component": "Battery",
  "params": { "mystery_param": 1 },
  "layout": { "x": 0, "y": 0 }
}
```

Invalid: unknown params are rejected.

### Wire with opaque path string

```json
{
  "id": "w1",
  "source": "root/node:a/port:v_out",
  "target": "root/node:b/port:v_in"
}
```

Invalid: wire endpoints must use structured `from` / `to` endpoint objects.

## Complete Example

```json
{
  "format": "blueprint",
  "version": 1,
  "blueprint_id": "aircraft.power.dc_main",
  "name": "DC Main",
  "interface": [
    { "id": "gnd",   "direction": "In",  "port_type": "V" },
    { "id": "v_in",  "direction": "In",  "port_type": "V" },
    { "id": "v_out", "direction": "Out", "port_type": "V" }
  ],
  "nodes": [
    {
      "id": "n_bat_01",
      "kind": "component",
      "label": "Battery 1",
      "component": "Battery",
      "params": {
        "voltage": 27.0
      },
      "layout": {
        "x": 120.0,
        "y": 80.0
      }
    },
    {
      "id": "n_panel_01",
      "kind": "blueprint_instance",
      "label": "12SAM28",
      "source": {
        "mode": "embedded",
        "blueprint": {
          "format": "blueprint",
          "version": 1,
          "blueprint_id": "embedded.12sam28",
          "name": "12SAM28",
          "interface": [
            { "id": "v_in", "direction": "In", "port_type": "V" },
            { "id": "v_out", "direction": "Out", "port_type": "V" }
          ],
          "nodes": [],
          "wires": []
        }
      },
      "collapsed": false,
      "layout": {
        "x": 420.0,
        "y": 80.0
      }
    },
    {
      "id": "n_tru_01",
      "kind": "blueprint_instance",
      "label": "TRU Panel",
      "source": {
        "mode": "reference",
        "blueprint_id": "library.power.tru_panel"
      },
      "layout": {
        "x": 760.0,
        "y": 80.0
      }
    }
  ],
  "wires": [
    {
      "id": "w_001",
      "from": { "node": "n_bat_01", "port": "v_out" },
      "to": { "node": "n_panel_01", "port": "v_in" },
      "routing": [[240.0, 96.0], [340.0, 96.0]]
    }
  ]
}
```

## Migration Stance

This spec defines the new format only.

- old-format files are invalid under this spec
- the canonical codec must not accept old-format fields
- any migration from old files must happen in a separate one-shot importer
- the importer is not part of canonical decode/encode

## Summary of Removed Old Concepts

The following old concepts are explicitly removed from canonical persistence:

- top-level `nested` collection
- host+nested same-id persisted pairing
- `owner_scope`
- `group_id`
- persisted host interface cache
- persisted referenced `resolved_iface`
- persisted `blueprint_path`
- string-encoded wire endpoints
- duplicated nested-sidecar instance position
- viewport/session/editor state in blueprint documents
