#!/bin/bash
# Font installer for NierTerm - Installs Meslo Nerd Font

set -e

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
echo "==> Installation complete! Restart NierTerm to use the new font."
