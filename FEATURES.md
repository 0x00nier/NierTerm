# NierTerm Features

## Core Features (Implemented)

- ✅ Pure C implementation
- ✅ PTY handling for bash shell
- ✅ Basic rendering system (GPU with CPU fallback)
- ✅ Window/pane resizing support
- ✅ Built-in autocomplete
- ✅ Built-in fuzzy finder (fzf-like)
- ✅ Tmux-like pane management
- ✅ Clipboard integration (system clipboard)
- ✅ Custom prompt: `<username>@<hostname> >`
- ✅ Autosuggestions
- ✅ History management

## Planned Features

- 🔄 SSH connection support
- 🔄 Enhanced GPU rendering with proper font textures
- 🔄 Nerd fonts and ligature support
- 🔄 Moonfly color scheme integration for syntax highlighting
- 🔄 Vim/Neovim workflow optimizations
- 🔄 Multi-threaded CPU rendering fallback
- 🔄 Better font rendering with FreeType
- 🔄 Improved input handling for special keys
- 🔄 Better terminal emulation (ANSI escape sequences)

## Architecture

### Components

1. **Terminal Core** (`terminal.c/h`)
   - Main terminal state management
   - Pane/window management
   - History and suggestions

2. **PTY Handler** (`pty.c/h`)
   - Pseudo-terminal creation and management
   - Shell spawning
   - Terminal resizing

3. **Renderer** (`renderer.c/h`)
   - GPU rendering (OpenGL)
   - CPU fallback rendering
   - Font rendering with FreeType
   - Buffer management

4. **Input Handler** (`input.c/h`)
   - Keyboard input processing
   - Key event handling
   - Modifier key support

5. **Autocomplete** (`autocomplete.c/h`)
   - Command completion
   - Path completion
   - History-based completion

6. **Fuzzy Finder** (`fuzzy.c/h`)
   - History search
   - Pattern matching
   - Scoring algorithm

7. **Autosuggestions** (`autosuggest.c/h`)
   - History-based suggestions
   - Prefix matching
   - Similarity scoring

8. **Clipboard** (`clipboard.c/h`)
   - System clipboard integration
   - X11 (Linux) and Cocoa (macOS) support

## Color Scheme

The terminal will support the Moonfly color scheme (https://github.com/bluz71/vim-moonfly-colors) for syntax highlighting. Colors are defined in `src/colors.h`.

## Building

```bash
make
```

## Running

```bash
./nierterm
```

## Key Bindings

- `Ctrl+C`: Send SIGINT to shell
- `Ctrl+V`: Paste from clipboard
- `Ctrl+F`: Open fuzzy finder
- `Alt+H`: Split pane horizontally
- `Alt+V`: Split pane vertically
- `Tab`: Autocomplete
- `Right Arrow`: Accept autosuggestion
- `ESC`: Close fuzzy finder

