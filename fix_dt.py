#!/usr/bin/env python3
"""Apply float dt parameter to all solve_* virtual methods throughout the codebase."""
import re, os

base = '/Users/vladimir/an24_cpp'

# ─── component.h ──────────────────────────────────────────────────────────────
component_h = os.path.join(base, 'src/jit_solver/component.h')
with open(component_h) as f:
    s = f.read()

old = '''\
    // TODO: CRITICAL - All solve_* methods MUST accept float dt parameter!
    // Currently they use hardcoded dt values which breaks physics when FPS changes.
    //
    // Change API to:
    //   virtual void solve_electrical(SimulationState& state, float dt) {}
    //   virtual void solve_mechanical(SimulationState& state, float dt) {}
    //   virtual void solve_thermal(SimulationState& state, float dt) {}
    //   virtual void solve_hydraulic(SimulationState& state, float dt) {}
    //
    // And update ALL component implementations to use passed dt instead of:
    //   constexpr float dt_mech = 0.05f;     // BAD! assumes 20 Hz
    //   constexpr float dt_thermal = 1.0f;   // BAD! assumes 1 Hz
    //
    // This is CRITICAL for correct simulation regardless of framerate!

    /// Solve electrical domain (60 Hz)
    virtual void solve_electrical(SimulationState& state) {}

    /// Solve hydraulic domain (5 Hz)
    virtual void solve_hydraulic(SimulationState& state) {}

    /// Solve mechanical domain (20 Hz)
    virtual void solve_mechanical(SimulationState& state) {}

    /// Solve thermal domain (1 Hz)
    virtual void solve_thermal(SimulationState& state) {}'''

new = '''\
    /// Solve electrical domain (every step, dt = frame delta)
    virtual void solve_electrical(SimulationState& state, float dt) {}

    /// Solve hydraulic domain (every 12th step, dt = 12 * frame delta)
    virtual void solve_hydraulic(SimulationState& state, float dt) {}

    /// Solve mechanical domain (every 3rd step, dt = 3 * frame delta)
    virtual void solve_mechanical(SimulationState& state, float dt) {}

    /// Solve thermal domain (every 60th step, dt = 60 * frame delta)
    virtual void solve_thermal(SimulationState& state, float dt) {}'''

assert old in s, "component.h: TODO block not found"
s = s.replace(old, new, 1)
with open(component_h, 'w') as f:
    f.write(s)
print('Updated component.h')

# ─── systems.h ────────────────────────────────────────────────────────────────
systems_h = os.path.join(base, 'src/jit_solver/systems.h')
with open(systems_h) as f:
    s = f.read()
s, n = re.subn(
    r'void solve_step\(SimulationState& state, size_t step\);',
    r'void solve_step(SimulationState& state, size_t step, float dt);',
    s)
assert n == 1, f"systems.h: expected 1 substitution, got {n}"
with open(systems_h, 'w') as f:
    f.write(s)
print('Updated systems.h')

# ─── systems.cpp ──────────────────────────────────────────────────────────────
systems_cpp = os.path.join(base, 'src/jit_solver/systems.cpp')
with open(systems_cpp) as f:
    s = f.read()

old = '''\
void Systems::solve_step(SimulationState& state, size_t step) {
    // TODO: CRITICAL BUG - hardcoded dt values!
    // All solve_* methods use hardcoded dt (0.05, 1.0, etc.) which breaks physics
    // when FPS changes. MUST pass real dt from simulation time instead of frame count.
    //
    // Solution:
    // 1. Change API: solve_step(state, step, dt) - pass real delta time
    // 2. Change Component::solve_* methods to accept dt parameter
    // 3. Replace all hardcoded dt with passed dt
    //
    // Current broken code:
    // - solve_mechanical: dt_mech = 0.05f (assumes exactly 20 Hz)
    // - solve_thermal: dt_thermal = 1.0f (assumes exactly 1 Hz)
    // - solve_hydraulic: likely has same issue
    //
    // This means if FPS drops from 60 to 30, everything moves 2x slower!
    // FIX ASAP!

    // Electrical: every step (60 Hz)
    for (auto& comp : electrical) {
        comp->solve_electrical(state);
    }

    // Mechanical: every 3rd step (20 Hz)
    if (step % 3 == 0) {
        size_t bucket = (step / 3) % 3;
        for (auto& comp : mechanical[bucket]) {
            comp->solve_mechanical(state);
        }
    }

    // Hydraulic: every 12th step (5 Hz)
    if (step % 12 == 0) {
        size_t bucket = (step / 12) % 12;
        for (auto& comp : hydraulic[bucket]) {
            comp->solve_hydraulic(state);
        }
    }

    // Thermal: every 60th step (1 Hz)
    if (step % 60 == 0) {
        for (auto& bucket : thermal) {
            for (auto& comp : bucket) {
                comp->solve_thermal(state);
            }
        }
    }
}'''

new = '''\
void Systems::solve_step(SimulationState& state, size_t step, float dt) {
    // Each domain receives accumulated dt for that domain's update period.
    const float dt_electrical = dt;           // every step
    const float dt_mechanical = dt * 3.0f;    // every 3rd step
    const float dt_hydraulic  = dt * 12.0f;   // every 12th step
    const float dt_thermal    = dt * 60.0f;   // every 60th step

    // Electrical: every step
    for (auto& comp : electrical) {
        comp->solve_electrical(state, dt_electrical);
    }

    // Mechanical: every 3rd step
    if (step % 3 == 0) {
        size_t bucket = (step / 3) % 3;
        for (auto& comp : mechanical[bucket]) {
            comp->solve_mechanical(state, dt_mechanical);
        }
    }

    // Hydraulic: every 12th step
    if (step % 12 == 0) {
        size_t bucket = (step / 12) % 12;
        for (auto& comp : hydraulic[bucket]) {
            comp->solve_hydraulic(state, dt_hydraulic);
        }
    }

    // Thermal: every 60th step
    if (step % 60 == 0) {
        for (auto& bucket : thermal) {
            for (auto& comp : bucket) {
                comp->solve_thermal(state, dt_thermal);
            }
        }
    }
}'''

assert old in s, "systems.cpp: solve_step block not found"
s = s.replace(old, new, 1)
with open(systems_cpp, 'w') as f:
    f.write(s)
print('Updated systems.cpp')

# ─── all.h ────────────────────────────────────────────────────────────────────
all_h = os.path.join(base, 'src/jit_solver/components/all.h')
with open(all_h) as f:
    s = f.read()

# Declared overrides (non-inline)
for method in ['solve_electrical', 'solve_hydraulic', 'solve_mechanical', 'solve_thermal']:
    s = re.sub(
        rf'void {method}\(SimulationState& state\) override;',
        rf'void {method}(SimulationState& state, float dt) override;',
        s)

# Inline Bus: void solve_electrical(SimulationState& state) override {
s = re.sub(
    r'(void solve_electrical\(SimulationState& state\)) override (\{[^}]*\})',
    r'\1, float /*dt*/) override \2',
    s)

with open(all_h, 'w') as f:
    f.write(s)
print('Updated all.h')

# ─── all.cpp ──────────────────────────────────────────────────────────────────
all_cpp = os.path.join(base, 'src/jit_solver/components/all.cpp')
with open(all_cpp) as f:
    s = f.read()

# All plain solve_* implementations get float /*dt*/ first
for method in ['solve_electrical', 'solve_hydraulic', 'solve_mechanical', 'solve_thermal']:
    s = re.sub(
        rf'(::{method})\(SimulationState& (state|st)\) \{{',
        rf'\1(SimulationState& \2, float /*dt*/) {{',
        s)

# RUG82::solve_electrical — needs dt for the integrator
old = '''\
void RUG82::solve_electrical(SimulationState& state, float /*dt*/) {'''
new = '''\
void RUG82::solve_electrical(SimulationState& state, float dt) {'''
s = s.replace(old, new, 1)

old = '    k_mod += kp * error * 0.01f;  // small time step factor'
new = '    k_mod += kp * error * dt;'
assert old in s, "RUG82 0.01f not found"
s = s.replace(old, new, 1)

# RU19A::solve_mechanical — remove hardcoded dt_mech = 0.05f, use dt
old = '''\
void RU19A::solve_mechanical(SimulationState& st, float /*dt*/) {'''
new = '''\
void RU19A::solve_mechanical(SimulationState& st, float dt) {'''
s = s.replace(old, new, 1)

old = '    // Mechanical solver runs at 20 Hz, so dt = 0.05 seconds\n    float dt_mech = 0.05f;\n'
new = ''
assert old in s, f"RU19A dt_mech not found"
s = s.replace(old, new, 1)

s = re.sub(r'dt_mech\b', 'dt', s)

# RU19A::solve_thermal — remove hardcoded dt_thermal = 1.0, use dt
old = '''\
void RU19A::solve_thermal(SimulationState& st, float /*dt*/) {'''
new = '''\
void RU19A::solve_thermal(SimulationState& st, float dt) {'''
s = s.replace(old, new, 1)

old = '    // Thermal solver runs at 1 Hz, so dt = 1.0 second\n    float dt_thermal = 1.0f;\n\n'
new = '\n'
assert old in s, "RU19A dt_thermal not found"
s = s.replace(old, new, 1)

s = re.sub(r'dt_thermal\b', 'dt', s)

with open(all_cpp, 'w') as f:
    f.write(s)
print('Updated all.cpp')
print('Done.')
