# NierTerm

A minimal, high-performance terminal emulator written in pure C.

## Features

- **Minimal & Fast**: Optimized for speed with GPU rendering (CPU fallback)
- **Built-in Features**: Autocomplete, autosuggestions, fuzzy finder, tmux-like panes
- **No Dependencies**: Fully self-contained (uses system libraries only)
- **Cross-platform**: Linux and macOS support
- **Modern**: Nerd fonts, ligatures, vim/neovim optimized
- **Color Scheme**: Moonfly color scheme support (https://github.com/bluz71/vim-moonfly-colors)
- **SSH Support**: Built-in SSH connection handling

## Building

```bash
make
```

## Running

```bash
./nierterm
```

## Requirements

- GCC or Clang
- OpenGL (for GPU rendering)
- FreeType2 (for font rendering)
- X11 (Linux) or Cocoa (macOS)

