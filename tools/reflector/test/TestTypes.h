#pragma once
#include <forge/TypeSystem.h>
#include <string>
#include <vector>
#include <cstdint>

namespace test {

FORGE_REFLECT_ENUM(Color)
enum class Color : uint8_t {
    Red   = 0,
    Green = 1,
    Blue  = 2,
};

struct SimpleStruct {
    FORGE_REFLECT_TYPE(SimpleStruct)

    FORGE_REFLECT()
    float x = 0.0f;

    FORGE_REFLECT()
    float y = 0.0f;

    FORGE_REFLECT()
    std::string name;

    FORGE_REFLECT()
    int32_t count = 0;
};

struct ContainerStruct {
    FORGE_REFLECT_TYPE(ContainerStruct)

    FORGE_REFLECT()
    std::vector<float> values;

    FORGE_REFLECT()
    int32_t id = 0;
};

} // namespace test
