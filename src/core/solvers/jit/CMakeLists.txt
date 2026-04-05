add_library(jit_solver
    jit_solver.cpp
    build_utils.cpp
    build_signals.cpp
    build_components.cpp
    build_components_logic.cpp
    build_components_control.cpp
    build_components_utility.cpp
    build_components_physical.cpp
    build_components_validation.cpp
    build_electrical.cpp
    simulator.cpp
    subsolvers/electrical_subsolver.cpp
    # Phase 2 Slice 1: Migrated push-style components
    components/switch.cpp
    components/relay.cpp
    components/hold_button.cpp
    components/ref_node.cpp
    components/generator.cpp
    components/bus.cpp
    components/blueprint_input.cpp
    components/blueprint_output.cpp
    # Phase 2 Slice 2: Migrated push-style components
    components/comparator.cpp
    components/current_sense.cpp
    components/azs.cpp
    components/resistor.cpp
    components/voltmeter.cpp
    components/indicator_light.cpp
    # Phase 2 Slice 3: Logical/math components
    components/add.cpp
    components/subtract.cpp
    components/multiply.cpp
    components/divide.cpp
    components/and_gate.cpp
    components/or_gate.cpp
    components/xor_gate.cpp
    components/not_gate.cpp
    components/nand_gate.cpp
    components/min.cpp
    components/max.cpp
    components/clamp.cpp
    # Phase 2 Slice 4: Control/filter components
    components/pid.cpp
    components/pi.cpp
    components/pd.cpp
    components/p.cpp
    components/accumulator.cpp
    components/integrator.cpp
    components/sample_hold.cpp
    components/time_delay.cpp
    components/monostable.cpp
    components/slew_rate.cpp
    components/asym_slew_rate.cpp
    components/fast_tmo.cpp
    components/asym_tmo.cpp
    # Phase 2 Slice 5: Signal-shaping / utility logical components
    components/normalize.cpp
    components/lut.cpp
    components/greater.cpp
    components/lesser.cpp
    components/greater_eq.cpp
    components/lesser_eq.cpp
    components/any_v_to_bool.cpp
    components/positive_v_to_bool.cpp
    components/lerp_node.cpp
    components/slider.cpp
    components/splitter.cpp
    components/merger.cpp
    # Phase 2 Slice 6: Migrated non-controlled components
    components/electric_heater.cpp
    components/electric_pump.cpp
    components/fuel_tank.cpp
    components/gidro_accumulator.cpp
    components/gyroscope.cpp
    components/inertia_node.cpp
    components/inverter.cpp
    components/radiator.cpp
    components/solenoid_valve.cpp
    components/spring.cpp
    components/temp_sensor.cpp
    components/transformer.cpp
    components/voltage_sense.cpp
    # Phase 2 Slice 7: Controlled source / conductance components
    components/controlled_voltage_source.cpp
    components/controlled_current_source.cpp
    components/variable_conductance.cpp
    # Step 13: Explicit primitive electrical nodes
    components/electrical_conductance.cpp
    components/electrical_source.cpp
    # Generic constant value source (non-electrical)
    components/value.cpp
    # Multi-position rotary selector switch
    components/knob_switch.cpp
)

target_include_directories(jit_solver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)

target_link_libraries(jit_solver PUBLIC
    spdlog::spdlog
    json_parser
)
