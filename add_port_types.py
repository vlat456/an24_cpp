#!/usr/bin/env python3
"""
Add port types to all component definitions.
"""
import json
import os
from pathlib import Path

# Port type mapping based on port names
PORT_TYPE_MAPPING = {
    # Voltage ports
    'v': 'V',
    'v_in': 'V',
    'v_out': 'V',
    'v_bus': 'V',
    'v_gen': 'V',
    'v_start': 'V',
    'v_gnd': 'V',
    'v_ref': 'V',
    'v_in_ref': 'V',
    'v_mod': 'V',

    # Current ports
    'i': 'I',
    'i_out': 'I',
    'i_in': 'I',

    # Boolean ports
    'closed': 'Bool',
    'open': 'Bool',
    'state': 'Bool',
    'lamp': 'Bool',
    'cmd': 'Bool',
    'enable': 'Bool',

    # RPM ports
    'rpm': 'RPM',
    'rpm_out': 'RPM',
    'rpm_in': 'RPM',
    'speed': 'RPM',

    # Temperature ports
    't': 'Temperature',
    't4': 'Temperature',
    't4_out': 'Temperature',
    'temp': 'Temperature',

    # Pressure ports
    'p': 'Pressure',
    'pressure': 'Pressure',
}

def infer_port_type(port_name: str) -> str:
    """Infer port type from port name."""
    port_name_lower = port_name.lower()

    # Direct matches
    for pattern, ptype in PORT_TYPE_MAPPING.items():
        if pattern in port_name_lower:
            return ptype

    # Special cases
    if 'k_' in port_name_lower:
        return 'V'  # Modulation signal
    if any(word in port_name_lower for word in ['on', 'off', 'enable', 'disable', 'start', 'stop']):
        return 'Bool'
    if 'pos' in port_name_lower or 'position' in port_name_lower:
        return 'Position'

    # Default
    return 'Any'

def update_component_file(filepath: Path):
    """Update component file with port types."""
    with open(filepath) as f:
        data = json.load(f)

    if 'default_ports' not in data:
        print(f"  No ports in {filepath.name}")
        return

    updated = False
    for port_name, port_def in data['default_ports'].items():
        if 'type' not in port_def:
            inferred_type = infer_port_type(port_name)
            port_def['type'] = inferred_type
            updated = True
            print(f"  {port_name}: {inferred_type}")

    if updated:
        with open(filepath, 'w') as f:
            json.dump(data, f, indent=2)
            # Add trailing newline
            f.write('\n')
        print(f"  Updated {filepath.name}")
    else:
        print(f"  All ports have types in {filepath.name}")

def main():
    components_dir = Path('components')
    json_files = sorted(components_dir.glob('*.json'))

    print(f"Found {len(json_files)} component files\n")

    for filepath in json_files:
        print(f"Processing {filepath.name}:")
        update_component_file(filepath)
        print()

if __name__ == '__main__':
    main()
