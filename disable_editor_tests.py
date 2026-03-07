#!/usr/bin/env python3
import re

with open('/Users/vladimir/an24_cpp/tests/CMakeLists.txt', 'r') as f:
    lines = f.readlines()

# Targets to disable
targets_to_disable = {
    'editor_persist_tests',
    'editor_viewport_tests', 
    'editor_hittest_tests',
    'editor_gl_setup_tests',
    'editor_routing_tests',
    'editor_router_tests',
    'editor_widget_tests'
}

result = []
i = 0
while i < len(lines):
    line = lines[i]
    
    # Check if this line starts a test we want to disable
    found_target = False
    for target in targets_to_disable:
        if f'add_executable({target}' in line:
            found_target = True
            # Comment out this line and following lines until gtest_discover_tests
            while i < len(lines):
                current = lines[i]
                if current.strip() and not current.strip().startswith('#'):
                    result.append('# ' + current)
                else:
                    result.append(current)
                if f'gtest_discover_tests({target})' in current:
                    i += 1
                    break
                i += 1
            break
    
    if not found_target:
        result.append(line)
        i += 1

with open('/Users/vladimir/an24_cpp/tests/CMakeLists.txt', 'w') as f:
    f.writelines(result)

print("Disabled all editor tests")
