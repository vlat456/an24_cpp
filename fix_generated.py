#!/usr/bin/env python3
"""Update auto-generated files to use float dt in solve_* and solve_step."""
import re
import os

base = '/Users/vladimir/an24_cpp/generated'

cpp_files = [
    'generated_an24_composite_test.cpp',
    'generated_gs24_test.cpp',
    'generated_lerp_test.cpp',
    'generated_vsu_dmr_test.cpp',
    'generated_vsu_test.cpp',
]

h_files = [
    'generated_an24_composite_test.h',
    'generated_gs24_test.h',
    'generated_lerp_test.h',
    'generated_vsu_dmr_test.h',
    'generated_vsu_test.h',
]

def update_cpp(content):
    content = re.sub(
        r'void Systems::solve_step\(void\* state, uint32_t step\)',
        r'void Systems::solve_step(void* state, uint32_t step, float dt)',
        content
    )
    content = re.sub(
        r'AOT_INLINE void Systems::step_(\d+)\(void\* state\)',
        r'AOT_INLINE void Systems::step_\1(void* state, float dt)',
        content
    )
    content = re.sub(
        r'(case \d+: step_\d+)\(state\);',
        r'\1(state, dt);',
        content
    )
    content = re.sub(r'\.solve_electrical\(\*st\);', r'.solve_electrical(*st, dt);', content)
    content = re.sub(r'\.solve_mechanical\(\*st\);', r'.solve_mechanical(*st, dt * 3.0f);', content)
    content = re.sub(r'\.solve_hydraulic\(\*st\);', r'.solve_hydraulic(*st, dt * 12.0f);', content)
    content = re.sub(r'\.solve_thermal\(\*st\);', r'.solve_thermal(*st, dt * 60.0f);', content)
    return content

def update_h(content):
    content = re.sub(
        r'void solve_step\(void\* state, uint32_t step\);',
        r'void solve_step(void* state, uint32_t step, float dt);',
        content
    )
    content = re.sub(
        r'AOT_INLINE void step_(\d+)\(void\* state\);',
        r'AOT_INLINE void step_\1(void* state, float dt);',
        content
    )
    return content

for fname in cpp_files:
    path = os.path.join(base, fname)
    with open(path, 'r') as f:
        content = f.read()
    new_content = update_cpp(content)
    if new_content != content:
        with open(path, 'w') as f:
            f.write(new_content)
        print('Updated ' + fname)
    else:
        print('No changes in ' + fname)

for fname in h_files:
    path = os.path.join(base, fname)
    with open(path, 'r') as f:
        content = f.read()
    new_content = update_h(content)
    if new_content != content:
        with open(path, 'w') as f:
            f.write(new_content)
        print('Updated ' + fname)
    else:
        print('No changes in ' + fname)

print('Done.')
