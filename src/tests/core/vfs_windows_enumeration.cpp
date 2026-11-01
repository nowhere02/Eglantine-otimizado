// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <winioctl.h>

#include <catch2/catch_test_macros.hpp>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "core/file_sys/vfs/vfs_real.h"

TEST_CASE("Real VFS enumerates Windows directory entries", "[core][vfs]") {
    namespace fs = std::filesystem;
    struct Fixture {
        fs::path dir = fs::temp_directory_path() /
                       (L"eglantine-vfs-\u00e1-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                        std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Fixture() {
            std::error_code ec;
            fs::remove(dir / L"file-link", ec);
            fs::remove(dir / L"dir-link", ec);
            fs::remove(dir / L"zero.bin", ec);
            fs::remove(dir / L"\u00e1.bin", ec);
            fs::remove(dir / L"large.bin", ec);
            fs::remove(dir / L"nested" / L"drive.bin", ec);
            fs::remove(dir / L"nested", ec);
            fs::remove(dir, ec);
        }
    } fixture;
    REQUIRE(fs::create_directory(fixture.dir));
    REQUIRE(fs::create_directory(fixture.dir / L"nested"));
    {
        std::ofstream empty{fixture.dir / L"zero.bin", std::ios::binary};
        REQUIRE(empty.good());
    }
    {
        std::ofstream file{fixture.dir / L"\u00e1.bin", std::ios::binary};
        file << "abc";
        REQUIRE(file.good());
    }

    std::error_code ec;
    fs::create_symlink(fixture.dir / L"\u00e1.bin", fixture.dir / L"file-link", ec);
    const bool file_link_created = !ec;
    ec.clear();
    fs::create_directory_symlink(fixture.dir / L"nested", fixture.dir / L"dir-link", ec);
    const bool dir_link_created = !ec;
    if (!file_link_created || !dir_link_created) {
        WARN("Some symbolic-link cases require Windows developer mode or link privileges");
    }

    FileSys::RealVfsFilesystem real_fs;
    const auto dir = real_fs.OpenDirectory(Common::FS::PathToUTF8String(fixture.dir));
    REQUIRE(dir);
    const auto files = dir->GetFiles();
    const auto directories = dir->GetSubdirectories();
    const std::vector<u8> expected{'a', 'b', 'c'};
    REQUIRE(files.size() == 2 + static_cast<size_t>(file_link_created));
    REQUIRE(directories.size() == 1 + static_cast<size_t>(dir_link_created));
    for (const auto& file : files) {
        REQUIRE(file);
        if (file->GetName() == "zero.bin") {
            REQUIRE(file->GetSize() == 0);
        } else {
            REQUIRE((file->GetName() == "\xc3\xa1.bin" || file->GetName() == "file-link"));
            REQUIRE(file->GetSize() == 3);
            REQUIRE(file->ReadAllBytes() == expected);
        }
    }
    for (const auto& child : directories) {
        REQUIRE((child->GetName() == "nested" || child->GetName() == "dir-link"));
        REQUIRE(child->GetFiles().empty());
        REQUIRE(child->GetSubdirectories().empty());
    }

    const auto large_path = fixture.dir / L"large.bin";
    const HANDLE large_handle = CreateFileW(large_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(large_handle != INVALID_HANDLE_VALUE);
    DWORD bytes_returned{};
    const bool sparse = DeviceIoControl(large_handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
                                        &bytes_returned, nullptr) != 0;
    constexpr u64 large_size = (u64{1} << 32) + 7;
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(large_size);
    const bool resized = sparse && SetFilePointerEx(large_handle, end, nullptr, FILE_BEGIN) &&
                         SetEndOfFile(large_handle);
    CloseHandle(large_handle);
    if (sparse && sizeof(std::size_t) >= sizeof(large_size)) {
        REQUIRE(resized);
        bool found_large = false;
        for (const auto& file : dir->GetFiles()) {
            if (file->GetName() == "large.bin") {
                REQUIRE(file->GetSize() == large_size);
                found_large = true;
            }
        }
        REQUIRE(found_large);
    } else if (!sparse) {
        WARN("The temporary filesystem does not support sparse files");
    }

    const auto drive = fixture.dir.root_name().string();
    if (drive.size() == 2 && drive[1] == ':') {
        {
            std::ofstream file{fixture.dir / L"nested" / L"drive.bin", std::ios::binary};
            file << "abc";
            REQUIRE(file.good());
        }
        struct RestoreCurrentDirectory {
            fs::path previous = fs::current_path();
            ~RestoreCurrentDirectory() {
                std::error_code error;
                fs::current_path(previous, error);
            }
        } restore;
        fs::current_path(fixture.dir);
        size_t expected_drive_files = 0;
        Common::FS::IterateDirEntries(
            drive, [&expected_drive_files](const fs::directory_entry&) {
                ++expected_drive_files;
                return true;
            }, Common::FS::DirEntryFilter::File);
        REQUIRE(real_fs.OpenDirectory(drive)->GetFiles().size() == expected_drive_files);

        const auto drive_dir = real_fs.OpenDirectory(drive + "nested");
        bool found = false;
        for (const auto& file : drive_dir->GetFiles()) {
            if (file->GetName() == "drive.bin") {
                REQUIRE(file->ReadAllBytes() == expected);
                found = true;
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("Benchmark real VFS directory traversal", "[!benchmark][core][vfs]") {
    const char* path = std::getenv("EGLANTINE_VFS_BENCH_DIR");
    if (!path || !*path) {
        SKIP("Set EGLANTINE_VFS_BENCH_DIR to a directory to benchmark");
    }

    FileSys::RealVfsFilesystem real_fs;
    std::vector<FileSys::VirtualDir> pending{real_fs.OpenDirectory(path)};
    std::vector<FileSys::VirtualFile> files;
    size_t directories = 0;
    u64 total_size = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!pending.empty()) {
        auto dir = std::move(pending.back());
        pending.pop_back();
        REQUIRE(dir);
        ++directories;
        for (auto& file : dir->GetFiles()) {
            REQUIRE(file);
            total_size += file->GetSize();
            files.push_back(std::move(file));
        }
        for (auto& child : dir->GetSubdirectories()) {
            pending.push_back(std::move(child));
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    REQUIRE(!files.empty());
    std::printf("VFS traversal: %zu directories, %zu files, %llu bytes, %lld ms\n", directories,
                files.size(), static_cast<unsigned long long>(total_size),
                static_cast<long long>(elapsed.count()));
}

#endif
