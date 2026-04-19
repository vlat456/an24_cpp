#include "jit_solver_internal.h"
#include "build_components_common.h"

namespace jit_solver_impl {

bool try_build_utility_component(
    BuildResult& result,
    const ResolvedDevice& dev,
    ParamReader& param_reader)
{
    if (dev.classname == "Normalize") {
        Normalize<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "LUT") {
        LUT<JitProvider> comp;
        if (auto it = dev.params.find("table"); it != dev.params.end()) {
            const std::string table = param_reader.consume_string_optional("table", "");
            std::vector<float> keys, vals;
            if (LUT<JitProvider>::parse_table(table, keys, vals)) {
                comp.table_offset = static_cast<uint32_t>(result.lut_keys.size());
                comp.table_size = static_cast<uint16_t>(keys.size());
                result.lut_keys.insert(result.lut_keys.end(), keys.begin(), keys.end());
                result.lut_values.insert(result.lut_values.end(), vals.begin(), vals.end());
            }
        }
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Greater") {
        Greater<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Lesser") {
        Lesser<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "GreaterEq") {
        GreaterEq<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "LesserEq") {
        LesserEq<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Any_V_to_Bool") {
        Any_V_to_Bool<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Positive_V_to_Bool") {
        Positive_V_to_Bool<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "LerpNode") {
        LerpNode<JitProvider> comp;
        comp.factor = param_reader.consume_float_required("factor");
        comp.deadzone = param_reader.consume_float_required("deadzone");
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Slider") {
        Slider<JitProvider> comp;
        comp.min = param_reader.consume_float_optional("min", 0.0f);
        comp.max = param_reader.consume_float_optional("max", 1.0f);
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Splitter") {
        Splitter<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }
    if (dev.classname == "Merger") {
        Merger<JitProvider> comp;
        setup_component_ports(result, dev, comp);
        register_component_consumer(result, dev, param_reader, std::move(comp));
        return true;
    }

    return false;
}

} // namespace jit_solver_impl
