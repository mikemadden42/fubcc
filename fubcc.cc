#include <CoreFoundation/CoreFoundation.h>
#include <libkern/OSByteOrder.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

enum BinaryType { Unknown, Universal, Intel, AppleSilicon };

struct ProgramInfo {
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
                      std::vector<ProgramInfo>& programs) {
    // Sort the vector alphabetically based on executableName
    std::sort(programs.begin(), programs.end(),
              [](const ProgramInfo& a, const ProgramInfo& b) {
                  return a.executableName < b.executableName;
              });

    std::cout << std::endl << title << ":" << std::endl;
    for (const auto& program : programs) {
        std::cout << program.executableName << std::endl;
    }
}

void listDirectoriesAndInfoPlist(const std::string& path) {
    std::vector<ProgramInfo> universalBinaries;
    std::vector<ProgramInfo> intelBinaries;
    std::vector<ProgramInfo> appleSiliconBinaries;

    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (fs::is_directory(entry.status())) {
                std::string directoryPath = entry.path().string();
                // std::cout << "Directory: " << directoryPath << std::endl;

                // Check for Contents/Info.plist
                std::string infoPlistPath =
                    directoryPath + "/Contents/Info.plist";
                if (fs::exists(infoPlistPath)) {
                    // std::cout << "Info.plist: " << infoPlistPath <<
                    // std::endl;
                    ProgramInfo programInfo;
                    if (!readBundleExecutable(directoryPath, programInfo)) {
                        continue;
                    }
                    programInfo.binaryType =
                        detectArch(programInfo.executablePath);

                    switch (programInfo.binaryType) {
                        case Universal:
                            universalBinaries.push_back(programInfo);
                            break;
                        case Intel:
                            intelBinaries.push_back(programInfo);
                            break;
                        case AppleSilicon:
                            appleSiliconBinaries.push_back(programInfo);
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        // Print each list
        printProgramList("Intel (x86_64) Binaries", intelBinaries);
        printProgramList("Apple Silicon (arm64) Binaries",
                         appleSiliconBinaries);
        printProgramList("Universal Binaries", universalBinaries);

    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error accessing directory: " << e.what() << std::endl;
    }
}

int main() {
    std::string path = "/Applications";  // Specify the directory path
    listDirectoriesAndInfoPlist(path);

    return 0;
}
