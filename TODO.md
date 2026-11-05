# TODO

## Correctness bugs

- [x] **64-bit Intel apps are labeled "Apple Silicon"** (`fubcc.cc:68-74`)
  `MH_MAGIC` means 32-bit Mach-O and `MH_MAGIC_64` means 64-bit for both x86_64
  and arm64, so the magic number doesn't identify the CPU. Every modern thin
  Intel app lands in the Apple Silicon list, and the Intel list can only hold
  32-bit binaries. Read the full `mach_header_64` and check `cputype`
  (`CPU_TYPE_X86_64` vs `CPU_TYPE_ARM64`).

- [x] **Apps with a binary Info.plist are skipped** (`fubcc.cc:20-30`)
  The regex only handles XML plists. Apps that use the binary `bplist00` format
  (Numbers, Pages, Keynote, Xcode, iMovie, GarageBand, Spotify, Kindle,
  WhatsApp, ...) fail with "CFBundleExecutable not found". Use CoreFoundation
  instead (`CFBundleCreate` + `CFBundleCopyExecutableURL`, or
  `CFPropertyListCreateWithData`) and link `-framework CoreFoundation`.

- [x] **"Universal" only means "fat"** (`fubcc.cc:61`)
  A fat binary can hold only x86_64 + i386, or a single architecture. Walk the
  `fat_arch` entries (big-endian, so use `OSSwapBigToHostInt32`) and confirm
  arm64 is present. Handle `FAT_MAGIC_64` too.

- [x] **Uninitialized read** (`fubcc.cc:58-59`)
  `execFile.read` isn't checked. A short or failed read leaves `magic`
  uninitialized, which is undefined behavior.

## Smaller issues

- [ ] Unknown apps are dropped without a trace. Add an "Unknown" list.
- [x] Info.plist is read and regex-parsed twice (`parseInfoPlist` and
  `fubcc.cc:135-139`). Return the name and type together.
- [x] `ProgramInfo::executablePath` holds the plist path, not the executable
  path (`fubcc.cc:142`).
- [x] Remove dead code: the redundant `seekg(0)` + re-read (`fubcc.cc:65-67`)
  and the `execFile.close()` that can never run (`fubcc.cc:81`).
- [ ] The search path is hard-coded to `/Applications` and the scan isn't
  recursive. It misses `/Applications/Utilities`, `~/Applications`, nested
  folders and `/System/Applications`. Take paths from `argv` and filter on the
  `.app` extension.
- [ ] Add the missing includes: `<algorithm>`, `<vector>`, `<string>`,
  `<cstdint>`.
- [ ] Switch back to `"\n"` from `std::endl` (it flushes on every line).
- [ ] Remove the commented-out debug `cout`s and Boost lines.

## Build (`CMakeLists.txt`)

- [ ] Replace `set(CMAKE_CXX_FLAGS ...)`, which overwrites user flags, with
  `target_compile_options(fubcc PRIVATE -Wall -Wextra -pedantic)`.
- [ ] Add `set(CMAKE_CXX_STANDARD_REQUIRED ON)`.
- [ ] The code only builds on macOS (`<mach-o/*.h>`). Add a
  `message(FATAL_ERROR ...)` on other platforms.

## Repo

- [ ] Expand the README with build and usage instructions and sample output.
