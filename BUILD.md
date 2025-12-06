# Building NierTerm

## Prerequisites

### Linux

```bash
# Ubuntu/Debian
sudo apt-get install build-essential libx11-dev libxrandr-dev libgl1-mesa-dev libglu1-mesa-dev libfreetype6-dev

# Fedora/RHEL
sudo dnf install gcc make libX11-devel libXrandr-devel mesa-libGL-devel mesa-libGLU-devel freetype-devel

# Arch Linux
sudo pacman -S base-devel libx11 libxrandr mesa glu freetype2
```

### macOS

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install freetype
```

## Building

```bash
make
```

This will create the `nierterm` executable in the current directory.

## Installing

```bash
sudo make install
```

This will install `nierterm` to `/usr/local/bin/`.

## Cleaning

```bash
make clean
```

## Cross-compiling for macOS (from Linux)

To cross-compile for macOS from Linux, you'll need:

1. Install osxcross: https://github.com/tpoechtrager/osxcross
2. Modify the Makefile to use the osxcross toolchain
3. Build with the macOS target

Example:
```bash
export PATH=/path/to/osxcross/target/bin:$PATH
make CC=x86_64-apple-darwin20.4-clang
```

## Troubleshooting

### Missing OpenGL libraries
If you get errors about missing OpenGL, ensure you have the Mesa development packages installed (Linux) or Xcode (macOS).

### Missing FreeType
If font rendering fails, ensure FreeType2 is installed and the font path in `config.h` is correct.

### PTY errors
Ensure your system supports `openpty()`. On some systems, you may need to link against `-lutil`.

