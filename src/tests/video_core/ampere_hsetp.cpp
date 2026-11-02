// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

TEST_CASE("Ampere half comparisons preserve register operands", "[video_core][ampere]") {
    using Shader::Ampere::Instruction;
    using Shader::Ampere::LowerToMaxwell;
    const Instruction compare{{0x34007234, 0x30000034, 0x00702800, 0x000fe400}};
    const Instruction exit{{0x0000794d, 0, 0x03800000, 0}};
    const auto recorded = LowerToMaxwell(std::array{compare, exit});
    REQUIRE(static_cast<bool>(recorded));
    REQUIRE(recorded.instructions.size() == 2);
    REQUIRE(recorded.instructions[0] == 0x5d21001033473407ULL);

    auto modified = compare;
    modified.words[1] |= 0xc0000000U;
    const auto with_modifiers = LowerToMaxwell(std::array{modified, exit});
    REQUIRE(static_cast<bool>(with_modifiers));
    REQUIRE(((with_modifiers.instructions[0] >> 30) & 3) == 3);

    modified = compare;
    modified.words[2] |= 2;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{modified, exit})));
}
