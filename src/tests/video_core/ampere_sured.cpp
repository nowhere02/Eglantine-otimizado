// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

TEST_CASE("Ampere surface reduction lowers the recorded compute instruction", "[video_core][ampere]") {
    using Shader::Ampere::Instruction;
    using Shader::Ampere::LowerToMaxwell;
    const Instruction reduction{{0x0400759f, 0x60000c00, 0x0010e000, 0x000fe200}};
    const Instruction exit{{0x0000794d, 0, 0x03800000, 0}};

    const auto lowered = LowerToMaxwell(std::array{reduction, exit});
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.required_texture_buffer == 0);
    REQUIRE(lowered.instructions.size() == 2);
    REQUIRE(lowered.instructions[0] == 0xeb4800c600070400ULL);

    auto unsupported = reduction;
    unsupported.words[2] |= 1;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, exit})));

    auto other_buffer = reduction;
    other_buffer.words[1] |= 1U << 22;
    const auto conflict = LowerToMaxwell(std::array{reduction, other_buffer, exit});
    REQUIRE_FALSE(static_cast<bool>(conflict));
    REQUIRE(conflict.error == "native surface descriptor buffer conflict");
}
