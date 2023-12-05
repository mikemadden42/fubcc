#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

enum BinaryType { Unknown, Universal, Intel, AppleSilicon };

struct ProgramInfo {
    std::string executableName;
    std::string executablePath;
    BinaryType binaryType;
};

std::string getCFBundleExecutable(const std::string& content) {
    std::regex regex(
        "<key>CFBundleExecutable</key>\\s*<string>([^<]+)</string>");
    std::smatch match;

    if (std::regex_search(content, match, regex) && match.size() == 2) {
        return match[1].str();
    }

    return "";
}

BinaryType parseInfoPlist(const std::string& infoPlistPath) {
    std::ifstream file(infoPlistPath,
                       std::ios::binary);  // Open the file in binary mode
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();

        std::string bundleExecutable = getCFBundleExecutable(content);

        if (!bundleExecutable.empty()) {
            // std::cout << "CFBundleExecutable: " << bundleExecutable <<
            // std::endl;

            // Construct the path to the executable file under macOS directory
            std::string executablePath = infoPlistPath;
            size_t pos = executablePath.find_last_of('/');
            executablePath =
                executablePath.substr(0, pos) + "/MacOS/" + bundleExecutable;

            // std::cout << "Executable Path: " << executablePath << std::endl;

            // Check the architecture of the executable
            std::ifstream execFile(executablePath, std::ios::binary);
            if (execFile.is_open()) {
                uint32_t magic;
                execFile.read(reinterpret_cast<char*>(&magic), sizeof(magic));

                if (magic == FAT_MAGIC || magic == FAT_CIGAM) {
                    // std::cout << "Universal Binary" << std::endl;
                    return Universal;
                } else {
                    execFile.seekg(0);  // Reset file position
                    execFile.read(reinterpret_cast<char*>(&magic),
                                  sizeof(magic));
                    if (magic == MH_MAGIC || magic == MH_CIGAM) {
                        // std::cout << "Intel (x86_64) Binary" << std::endl;
                        return Intel;
                    } else if (magic == MH_MAGIC_64 || magic == MH_CIGAM_64) {
                        // std::cout << "Apple Silicon (arm64) Binary" <<
                        // std::endl;
                        return AppleSilicon;
                    } else {
                        std::cerr << "Unknown Binary Format" << std::endl;
                        return Unknown;
                    }
                }

                execFile.close();
            } else {
                std::cerr << "Error opening executable file: " << executablePath
                          << std::endl;
                return Unknown;
            }
        } else {
            std::cerr << "Error: CFBundleExecutable not found in "
                      << infoPlistPath << std::endl;
            return Unknown;
        }
    } else {
        std::cerr << "Error opening Info.plist file: " << infoPlistPath
                  << std::endl;
        return Unknown;
    }
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
                    BinaryType binaryType = parseInfoPlist(infoPlistPath);

                    ProgramInfo programInfo;

                    // Read the contents of Info.plist
                    std::ifstream file(infoPlistPath);
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    programInfo.executableName =
                        getCFBundleExecutable(buffer.str());
                    file.close();

                    programInfo.executablePath = infoPlistPath;
                    programInfo.binaryType = binaryType;

                    switch (binaryType) {
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
