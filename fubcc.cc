#include <CoreFoundation/CoreFoundation.h>
#include <libkern/OSByteOrder.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>

namespace fs = std::filesystem;

enum BinaryType { Unknown, Universal, Intel, AppleSilicon };

struct ProgramInfo {
    fs::path bundlePath;
    std::string executableName;
    std::string executablePath;
    BinaryType binaryType;
};

BinaryType archFromCpuType(cpu_type_t cpuType) {
    switch (cpuType) {
        case CPU_TYPE_X86_64:
            return Intel;
        case CPU_TYPE_ARM64:
            return AppleSilicon;
        default:
            return Unknown;
    }
}

BinaryType detectArch(const std::string& executablePath) {
    std::ifstream file(executablePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error opening executable file: " << executablePath
                  << std::endl;
        return Unknown;
    }

    uint32_t magic;
    if (!file.read(reinterpret_cast<char*>(&magic), sizeof(magic))) {
        std::cerr << "Error reading executable file: " << executablePath
                  << std::endl;
        return Unknown;
    }
    file.seekg(0);

    // Fat headers are always big-endian on disk, whatever the host order.
    if (magic == FAT_MAGIC || magic == FAT_CIGAM || magic == FAT_MAGIC_64 ||
        magic == FAT_CIGAM_64) {
        fat_header header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            std::cerr << "Truncated fat header: " << executablePath
                      << std::endl;
            return Unknown;
        }

        bool is64 = magic == FAT_MAGIC_64 || magic == FAT_CIGAM_64;
        uint32_t archCount = OSSwapBigToHostInt32(header.nfat_arch);
        bool hasIntel = false;
        bool hasAppleSilicon = false;

        for (uint32_t i = 0; i < archCount; ++i) {
            cpu_type_t cpuType;
            if (is64) {
                fat_arch_64 arch;
                if (!file.read(reinterpret_cast<char*>(&arch), sizeof(arch))) {
                    break;
                }
                cpuType = OSSwapBigToHostInt32(arch.cputype);
            } else {
                fat_arch arch;
                if (!file.read(reinterpret_cast<char*>(&arch), sizeof(arch))) {
                    break;
                }
                cpuType = OSSwapBigToHostInt32(arch.cputype);
            }

            BinaryType type = archFromCpuType(cpuType);
            hasIntel |= type == Intel;
            hasAppleSilicon |= type == AppleSilicon;
        }

        if (hasIntel && hasAppleSilicon) {
            return Universal;
        } else if (hasAppleSilicon) {
            return AppleSilicon;
        } else if (hasIntel) {
            return Intel;
        }
        std::cerr << "No x86_64 or arm64 slice in: " << executablePath
                  << std::endl;
        return Unknown;
    }

    // mach_header and mach_header_64 share the same leading fields, so the
    // 32-bit struct is enough to read cputype from either.
    if (magic == MH_MAGIC || magic == MH_CIGAM || magic == MH_MAGIC_64 ||
        magic == MH_CIGAM_64) {
        mach_header header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            std::cerr << "Truncated Mach-O header: " << executablePath
                      << std::endl;
            return Unknown;
        }

        bool swapped = magic == MH_CIGAM || magic == MH_CIGAM_64;
        cpu_type_t cpuType =
            swapped ? OSSwapInt32(header.cputype) : header.cputype;
        return archFromCpuType(cpuType);
    }

    std::cerr << "Unknown Binary Format: " << executablePath << std::endl;
    return Unknown;
}

// Uses CoreFoundation to locate the bundle's executable, which handles both
// XML and binary Info.plist files.
bool readBundleExecutable(const std::string& bundlePath, ProgramInfo& info) {
    CFURLRef bundleURL = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bundlePath.c_str()),
        bundlePath.size(), true);
    if (!bundleURL) {
        return false;
    }

    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, bundleURL);
    CFRelease(bundleURL);
    if (!bundle) {
        std::cerr << "Error opening bundle: " << bundlePath << std::endl;
        return false;
    }

    CFURLRef executableURL = CFBundleCopyExecutableURL(bundle);
    CFRelease(bundle);
    if (!executableURL) {
        std::cerr << "Error: no executable found in " << bundlePath
                  << std::endl;
        return false;
    }

    char path[PATH_MAX];
    bool ok = CFURLGetFileSystemRepresentation(
        executableURL, true, reinterpret_cast<UInt8*>(path), sizeof(path));
    CFRelease(executableURL);
    if (!ok) {
        std::cerr << "Error resolving executable path in " << bundlePath
                  << std::endl;
        return false;
    }

    info.executablePath = path;
    info.executableName = fs::path(path).filename().string();
    return true;
}

void printProgramList(const std::string& title,
                      const std::vector<ProgramInfo>& programs,
                      BinaryType binaryType) {
    std::cout << std::endl << title << ":" << std::endl;
    for (const auto& program : programs) {
        if (program.binaryType == binaryType) {
            std::cout << program.executableName << std::endl;
        }
    }
}

bool isAppBundle(const fs::path& path) {
    std::error_code ec;
    return path.extension() == ".app" && fs::is_directory(path, ec);
}

void addBundle(const fs::path& bundlePath, std::vector<ProgramInfo>& programs) {
    ProgramInfo programInfo;
    std::error_code ec;
    programInfo.bundlePath = fs::weakly_canonical(bundlePath, ec);
    if (ec) {
        programInfo.bundlePath = bundlePath;
    }
    if (readBundleExecutable(bundlePath.string(), programInfo)) {
        programInfo.binaryType = detectArch(programInfo.executablePath);
    } else {
        // Keep the bundle so it shows up in the Unknown list.
        programInfo.executableName = bundlePath.stem().string();
        programInfo.binaryType = Unknown;
    }
    programs.push_back(programInfo);
}

// Recursively collects .app bundles under root. Bundles are not descended
// into, so helper apps nested inside an app are skipped.
bool scanPath(const fs::path& root, std::vector<ProgramInfo>& programs) {
    if (isAppBundle(root)) {
        addBundle(root, programs);
        return true;
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::cerr << "Error accessing directory: " << root.string() << ": "
                  << ec.message() << std::endl;
        return false;
    }

    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (isAppBundle(it->path())) {
            addBundle(it->path(), programs);
            it.disable_recursion_pending();
        }
        it.increment(ec);
        if (ec) {
            std::cerr << "Error scanning " << root.string() << ": "
                      << ec.message() << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::vector<fs::path> paths(argv + 1, argv + argc);
    if (paths.empty()) {
        paths.push_back("/Applications");
    }

    std::vector<ProgramInfo> programs;
    bool ok = true;
    for (const auto& path : paths) {
        ok &= scanPath(path, programs);
    }

    std::sort(programs.begin(), programs.end(),
              [](const ProgramInfo& a, const ProgramInfo& b) {
                  return std::tie(a.executableName, a.bundlePath) <
                         std::tie(b.executableName, b.bundlePath);
              });

    // Overlapping paths can reach the same bundle more than once.
    programs.erase(std::unique(programs.begin(), programs.end(),
                               [](const ProgramInfo& a, const ProgramInfo& b) {
                                   return a.bundlePath == b.bundlePath;
                               }),
                   programs.end());

    printProgramList("Intel (x86_64) Binaries", programs, Intel);
    printProgramList("Apple Silicon (arm64) Binaries", programs, AppleSilicon);
    printProgramList("Universal Binaries", programs, Universal);
    printProgramList("Unknown", programs, Unknown);

    return ok ? 0 : 1;
}
