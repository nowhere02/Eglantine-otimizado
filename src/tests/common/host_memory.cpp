// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <array>
#include <barrier>
#include <thread>
#include <windows.h>
#endif

#include "common/host_memory.h"
#include "common/literals.h"

using Common::HostMemory;
using namespace Common::Literals;

static constexpr size_t VIRTUAL_SIZE = 1ULL << 39;
static constexpr size_t BACKING_SIZE = 4_GiB;
static constexpr auto PERMS = Common::MemoryPermission::ReadWrite;
static constexpr auto HEAP = false;

#ifdef _WIN32
TEST_CASE("HostMemory: large backing is committed on demand", "[common][host-memory-lazy]") {
    HostMemory mem(20_GiB, VIRTUAL_SIZE);
    auto* backing = mem.BackingBasePointer();
    REQUIRE(backing != nullptr);
    MEMORY_BASIC_INFORMATION info{};
    REQUIRE(VirtualQuery(backing, &info, sizeof(info)) != 0);
    REQUIRE(info.State == MEM_RESERVE);
    REQUIRE(info.Type == MEM_MAPPED);

    volatile u8* first_page = backing;
    SetLastError(ERROR_CRC);
    first_page[0] = 23;
    const DWORD error_after_fault = GetLastError();
    REQUIRE(error_after_fault == ERROR_CRC);
    REQUIRE(backing[0] == 23);
    REQUIRE(VirtualQuery(backing, &info, sizeof(info)) != 0);
    REQUIRE(info.State == MEM_COMMIT);
    REQUIRE(VirtualQuery(backing + 20_GiB - 1, &info, sizeof(info)) != 0);
    REQUIRE(info.State == MEM_RESERVE);

    mem.Map(0x5000, 0x100000, 0x3000, PERMS, HEAP);
    volatile u8* view = mem.VirtualBasePointer() + 0x5000;
    view[0x1234] = 76;
    REQUIRE(backing[0x101234] == 76);
}

TEST_CASE("HostMemory: concurrent first touches preserve shared mappings", "[common][host-memory-lazy]") {
    HostMemory mem(4_MiB, VIRTUAL_SIZE);
    for (size_t wave = 0; wave < 32; ++wave) {
        const size_t view_offset = 0x10000 + wave * 0x10000;
        const size_t backing_offset = 0x100000 + wave * 0x10000;
        mem.Map(view_offset, backing_offset, 0x1000, PERMS, HEAP);
        auto* view = reinterpret_cast<volatile u64*>(mem.VirtualBasePointer() + view_offset);
        std::barrier start{8};
        std::array<std::jthread, 8> threads;
        for (size_t index = 0; index < threads.size(); ++index) {
            threads[index] = std::jthread{[&, index] {
                start.arrive_and_wait();
                view[index] = index + 10;
            }};
        }
        for (auto& thread : threads) {
            thread.join();
        }
        const auto* backing = reinterpret_cast<const u64*>(mem.BackingBasePointer() + backing_offset);
        for (size_t index = 0; index < threads.size(); ++index) {
            REQUIRE(backing[index] == index + 10);
        }
    }
}

TEST_CASE("HostMemory: protection can precede first access", "[common][host-memory-lazy]") {
    HostMemory mem(4_MiB, VIRTUAL_SIZE);
    mem.Map(0x10000, 0x20000, 0x3000, PERMS, HEAP);
    mem.Protect(0x10000, 0x3000, Common::MemoryPermission::Read);
    volatile u8* view = mem.VirtualBasePointer() + 0x10000;
    REQUIRE(view[0] == 0);
    MEMORY_BASIC_INFORMATION info{};
    REQUIRE(VirtualQuery(const_cast<u8*>(view), &info, sizeof(info)) != 0);
    REQUIRE(info.Protect == PAGE_READONLY);
    mem.Protect(0x10000, 0x3000, PERMS);
    view[0] = 57;
    REQUIRE(mem.BackingBasePointer()[0x20000] == 57);
}

TEST_CASE("HostMemory: zeroing preserves untouched backing and clears dirty aliases",
          "[common][host-memory-lazy]") {
    HostMemory mem(4_MiB, VIRTUAL_SIZE);
    auto* backing = mem.BackingBasePointer();
    mem.ClearBackingRegion(0, 4_MiB, 0);
    MEMORY_BASIC_INFORMATION info{};
    REQUIRE(VirtualQuery(backing, &info, sizeof(info)) != 0);
    REQUIRE(info.State == MEM_RESERVE);

    mem.Map(0x10000, 0, 0x1000, PERMS, HEAP);
    volatile u8* view = mem.VirtualBasePointer() + 0x10000;
    view[0x180] = 91;
    mem.ClearBackingRegion(0x100, 0x100, 0);
    REQUIRE(view[0x180] == 0);
    mem.ClearBackingRegion(0x100, 0x100, 0xab);
    REQUIRE(view[0x100] == 0xab);
    REQUIRE(view[0xff] == 0);
    REQUIRE(view[0x200] == 0);
    REQUIRE(VirtualQuery(backing + 4_MiB - 1, &info, sizeof(info)) != 0);
    REQUIRE(info.State == MEM_RESERVE);
}
#endif

TEST_CASE("HostMemory: Initialize and deinitialize", "[common]") {
    {
        HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
        REQUIRE(mem.BackingBasePointer() != nullptr);
    }
    {
        HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
        REQUIRE(mem.BackingBasePointer() != nullptr);
    }
}

TEST_CASE("HostMemory: Simple map", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x5000, 0x8000, 0x1000, PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + 0x5000;
    data[0] = 50;
    REQUIRE(data[0] == 50);
}

TEST_CASE("HostMemory: Simple mirror map", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);
    mem.Map(0x8000, 0x4000, 0x1000, PERMS, HEAP);

    volatile u8* const mirror_a = mem.VirtualBasePointer() + 0x5000;
    volatile u8* const mirror_b = mem.VirtualBasePointer() + 0x8000;
    mirror_b[0] = 76;
    REQUIRE(mirror_a[0x1000] == 76);
}

TEST_CASE("HostMemory: Simple unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + 0x5000;
    data[75] = 50;
    REQUIRE(data[75] == 50);

    mem.Unmap(0x5000, 0x2000, HEAP);
}

TEST_CASE("HostMemory: Simple unmap and remap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + 0x5000;
    data[0] = 50;
    REQUIRE(data[0] == 50);

    mem.Unmap(0x5000, 0x2000, HEAP);

    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);
    REQUIRE(data[0] == 50);

    mem.Map(0x7000, 0x2000, 0x5000, PERMS, HEAP);
    REQUIRE(data[0x3000] == 50);
}

TEST_CASE("HostMemory: Nieche allocation", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x0000, 0, 0x20000, PERMS, HEAP);
    mem.Unmap(0x0000, 0x4000, HEAP);
    mem.Map(0x1000, 0, 0x2000, PERMS, HEAP);
    mem.Map(0x3000, 0, 0x1000, PERMS, HEAP);
    mem.Map(0, 0, 0x1000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Full unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x8000, 0x4000, HEAP);
    mem.Map(0x6000, 0, 0x16000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Right out of bounds unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x0000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x2000, 0x4000, HEAP);
    mem.Map(0x2000, 0x80000, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Left out of bounds unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);

    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x6000, 0x4000, HEAP);
    mem.Map(0x8000, 0, 0x2000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Multiple placeholder unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x0000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x4000, 0, 0x1b000, PERMS, HEAP);
    mem.Unmap(0x3000, 0x1c000, HEAP);
    mem.Map(0x3000, 0, 0x20000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap between placeholders", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x0000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x2000, 0x4000, HEAP);
    mem.Map(0x2000, 0, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap to origin", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x4000, 0x4000, HEAP);
    mem.Map(0, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap to right", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x8000, 0x4000, HEAP);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Partial right unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x4000, 0x10000, 0x4000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x1000] = 17;

    mem.Unmap(0x6000, 0x2000, HEAP);

    REQUIRE(ptr[0x1000] == 17);
}

TEST_CASE("HostMemory: Partial left unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x4000, 0x10000, 0x4000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x3000] = 19;
    ptr[0x3fff] = 12;

    mem.Unmap(0x4000, 0x2000, HEAP);

    REQUIRE(ptr[0x3000] == 19);
    REQUIRE(ptr[0x3fff] == 12);
}

TEST_CASE("HostMemory: Partial middle unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x4000, 0x10000, 0x4000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x0000] = 19;
    ptr[0x3fff] = 12;

    mem.Unmap(0x1000, 0x2000, HEAP);

    REQUIRE(ptr[0x0000] == 19);
    REQUIRE(ptr[0x3fff] == 12);
}

TEST_CASE("HostMemory: Partial sparse middle unmap and check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(0x4000, 0x10000, 0x2000, PERMS, HEAP);
    mem.Map(0x6000, 0x20000, 0x2000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x0000] = 19;
    ptr[0x3fff] = 12;

    mem.Unmap(0x5000, 0x2000, HEAP);

    REQUIRE(ptr[0x0000] == 19);
    REQUIRE(ptr[0x3fff] == 12);
}
