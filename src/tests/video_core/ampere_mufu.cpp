// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

TEST_CASE("Ampere special functions read uniform registers", "[video_core][ampere]") {
    using Shader::Ampere::Instruction;
    using Shader::Ampere::LowerToMaxwell;
    const Instruction exit{{0x0000794d, 0, 0x03800000, 0}};
    const std::array samples{
        Instruction{{0x00187d08, 8, 0x08001000, 0x000e6600}},
        Instruction{{0x000c7d08, 7, 0x08001000, 0x000e2200}},
        Instruction{{0x000c7d08, 6, 0x08001000, 0x000e2200}},
    };

    for (const auto& instruction : samples) {
        const auto lowered = LowerToMaxwell(std::array{instruction, exit});
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(lowered.instructions.size() == 2);
        REQUIRE(((lowered.instructions[0] >> 8) & 0xff) == 216 + instruction.words[1]);
    }

    auto unsupported = samples[0];
    unsupported.words[1] = 28;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, exit})));
    unsupported = samples[0];
    unsupported.words[2] &= ~0x08000000U;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, exit})));
    unsupported = samples[0];
    unsupported.words[0] = (unsupported.words[0] & ~0x00ff0000U) | (216U << 16);
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, exit})));
}
