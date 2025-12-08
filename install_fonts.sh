#!/bin/bash
# Setup script for NierTerm - Installs dependencies and fonts

set -e

echo "========================================"
echo "       NierTerm Setup Script"
echo "========================================"
echo

# Detect package manager and install dependencies
install_deps() {
    echo "==> Installing build dependencies..."

    if command -v apt &> /dev/null; then
        # Debian/Ubuntu
        sudo apt update
        sudo apt install -y libx11-dev libxft-dev libfreetype6-dev \
            fontconfig unzip build-essential
    elif command -v dnf &> /dev/null; then
        # Fedora
        sudo dnf install -y libX11-devel libXft-devel freetype-devel \
            fontconfig unzip gcc make
    elif command -v pacman &> /dev/null; then
        # Arch
        sudo pacman -S --noconfirm libx11 libxft freetype2 \
            fontconfig unzip base-devel
    elif command -v zypper &> /dev/null; then
        # openSUSE
        sudo zypper install -y libX11-devel libXft-devel freetype2-devel \
            fontconfig unzip gcc make
    else
        echo "Warning: Unknown package manager. Please install manually:"
        echo "  - libx11-dev (or equivalent)"
        echo "  - libxft-dev (or equivalent)"
        echo "  - libfreetype6-dev (or equivalent)"
        echo "  - fontconfig"
        echo "  - unzip"
    fi
}

# Install dependencies first
install_deps

echo
echo "==> Installing Meslo Nerd Font for NierTerm"

# Create fonts directory
FONT_DIR="$HOME/.local/share/fonts"
mkdir -p "$FONT_DIR"

cd "$FONT_DIR"

# Download latest Meslo Nerd Font
echo "==> Downloading Meslo Nerd Font..."
MESLO_URL="https://github.com/ryanoasis/nerd-fonts/releases/latest/download/Meslo.zip"

if command -v wget &> /dev/null; then
    wget -q --show-progress "$MESLO_URL" -O Meslo.zip
elif command -v curl &> /dev/null; then
    curl -L "$MESLO_URL" -o Meslo.zip
else
    echo "Error: Neither wget nor curl found. Please install one of them."
    exit 1
fi

# Extract
echo "==> Extracting fonts..."
unzip -o Meslo.zip

# Clean up
rm Meslo.zip

# Rebuild font cache
echo "==> Updating font cache..."
fc-cache -fv "$FONT_DIR" &> /dev/null

# Verify installation
echo "==> Verifying installation..."
if fc-list | grep -qi "meslo"; then
    echo "✓ Meslo Nerd Font installed successfully!"
    echo
    echo "Installed fonts:"
    fc-list | grep -i "meslo" | head -n 5
else
    echo "⚠ Warning: Font installed but not detected in cache."
    echo "   You may need to restart your system or run: fc-cache -fv"
fi

echo
echo "========================================"
echo "       Setup Complete!"
echo "========================================"
echo
echo "Now rebuild NierTerm with Xft support:"
echo "  make clean && make"
echo
echo "Then run:"
echo "  ./nierterm"
echo
echo "The terminal should now use Meslo Nerd Font!"
