// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Shader {

[[nodiscard]] constexpr std::size_t CompilationWorkerCount(std::size_t logical_processors,
                                                          std::uint64_t physical_memory) {
    const std::size_t cpu_workers = std::max<std::size_t>(logical_processors / 2, 1);
    if (physical_memory == 0) {
        return cpu_workers;
    }

    // Leave room for the running title and the host; compiler IR and driver jobs are sizable.
    constexpr std::uint64_t ReservedMemory = 8ULL << 30;
    constexpr std::uint64_t MemoryPerWorker = 1ULL << 30;
    const std::uint64_t compiler_memory =
        physical_memory > ReservedMemory ? physical_memory - ReservedMemory : 0;
    const std::uint64_t memory_workers =
        std::max<std::uint64_t>(compiler_memory / MemoryPerWorker, 1);
    return static_cast<std::size_t>(std::min<std::uint64_t>(cpu_workers, memory_workers));
}

} // namespace Shader
