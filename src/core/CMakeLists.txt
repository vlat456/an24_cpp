add_library(core
    # Build system (split into modular files)
    solvers/jit/build_utils.cpp
    solvers/jit/device_factory.cpp
    solvers/jit/jit_solver_main.cpp
    
    # State and simulation
    solvers/shared/state.cpp
    solvers/shared/electrical_subsolver.cpp
    simulator.cpp
    
    # Components
    components/switch.cpp
    components/relay.cpp
    components/hold_button.cpp
    components/load.cpp
    components/ref_node.cpp
    components/generator.cpp
    components/bus.cpp
    components/blueprint_input.cpp
    components/blueprint_output.cpp
    components/comparator.cpp
    components/current_sense.cpp
    components/azs.cpp
    components/resistor.cpp
    components/voltmeter.cpp
    components/indicator_light.cpp
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
    components/electric_heater.cpp
    components/electric_pump.cpp
    components/fuel_tank.cpp
    components/gidro_accumulator.cpp
    components/gyroscope.cpp
    components/high_power_load.cpp
    components/inertia_node.cpp
    components/inverter.cpp
    components/radiator.cpp
    components/solenoid_valve.cpp
    components/spring.cpp
    components/temp_sensor.cpp
    components/transformer.cpp
    components/voltage_sense.cpp
    components/controlled_voltage_source.cpp
    components/controlled_current_source.cpp
    components/variable_conductance.cpp
    components/electrical_conductance.cpp
    components/electrical_source.cpp
    components/value.cpp
    components/knob_switch.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)

target_link_libraries(core PUBLIC
    spdlog::spdlog
    json_parser
)