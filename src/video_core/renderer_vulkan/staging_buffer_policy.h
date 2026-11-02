// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <span>

namespace Vulkan {

inline constexpr std::size_t LargeStagingBufferSize = 16ULL << 20;

[[nodiscard]] constexpr std::size_t StagingBufferAllocationSize(std::size_t requested) {
    if (requested < LargeStagingBufferSize) {
        return std::bit_ceil((std::max)(requested, std::size_t{1}));
    }
    constexpr std::size_t granularity = 64ULL << 10;
    const std::size_t padding = (granularity - requested % granularity) % granularity;
    return padding <= (std::numeric_limits<std::size_t>::max)() - requested
               ? requested + padding
               : requested;
}

template <typename T>
[[nodiscard]] std::span<T> StagingBufferMappedRange(std::span<T> mapping, std::size_t requested) {
    return mapping.empty() ? mapping : mapping.first(requested);
}

class StagingBufferIdleBudget {
public:
    [[nodiscard]] bool ShouldRelease(std::size_t capacity, bool deferred, bool gpu_complete) {
        if (deferred || !gpu_complete) {
            return false;
        }
        if (capacity > MaxIdleBytes - retained_bytes) {
            return true;
        }
        retained_bytes += capacity;
        return false;
    }

private:
    static constexpr std::size_t MaxIdleBytes = 64ULL << 20;
    std::size_t retained_bytes{};
};

} // namespace Vulkan
