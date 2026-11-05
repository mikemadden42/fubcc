# fubcc

Find universal binaries in C++.

fubcc scans folders for macOS `.app` bundles and sorts them by the
architectures their main executable supports:

- **Intel (x86_64)**: runs on Apple Silicon only through Rosetta 2
- **Apple Silicon (arm64)**: native on Apple Silicon only
- **Universal**: contains both x86_64 and arm64
- **Unknown**: no executable found, or no x86_64 or arm64 code

It reads the Mach-O headers directly and handles both XML and binary
`Info.plist` files.

## Building

Requires macOS, CMake 3.10+, and a C++17 compiler (Xcode or the Command Line
Tools).

```sh
cmake -S . -B build
cmake --build build
```

## Usage

```sh
fubcc [path...]
```

Each path can be a folder, which is searched recursively, or a single `.app`
bundle. With no arguments, fubcc scans `/Applications`. Apps nested inside
another app's bundle are not listed.

```sh
./build/fubcc
./build/fubcc /Applications /System/Applications ~/Applications
./build/fubcc /Applications/Safari.app
```

fubcc exits with status 1 if any path could not be scanned.

## Example output

```
Intel (x86_64) Binaries:

Apple Silicon (arm64) Binaries:
Slack
Spotify
Xcode

Universal Binaries:
Keynote
Numbers
Safari

Unknown:
```
