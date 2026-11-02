// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

TEST_CASE("Ampere indexed constant loads retain narrow sizes", "[video_core][ampere]") {
    using Shader::Ampere::Instruction;
    using Shader::Ampere::LowerToMaxwell;
    const Instruction load{{0xff139b82, 0x03c00800, 0, 0x000e6200}};
    const Instruction exit{{0x0000794d, 0, 0x03800000, 0}};

    const auto recorded = LowerToMaxwell(std::array{load, exit});
    REQUIRE(static_cast<bool>(recorded));
    REQUIRE(recorded.instructions.size() == 2);
    REQUIRE(recorded.instructions[0] == 0xef9000f00209ff13ULL);

    for (u32 width = 0; width <= 5; ++width) {
        auto variant = load;
        variant.words[2] = width << 9;
        if (width == 5) {
            variant.words[0] = 0xff129b82;
        }
        const auto lowered = LowerToMaxwell(std::array{variant, exit});
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(((lowered.instructions[0] >> 48) & 7) == width);
    }

    auto unsupported = load;
    unsupported.words[2] = 6U << 9;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, exit})));
    unsupported.words[2] = 5U << 9;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, exit})));
}
