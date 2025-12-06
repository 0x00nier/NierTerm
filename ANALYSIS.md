# NierTerm Codebase Analysis

## Critical Issues

### 1. Memory Leak in terminal_create() (terminal.c:50)
- PTY structure is freed immediately after creation, but master_fd is still used
- **Fix**: Don't free the PTY structure, or extract values before freeing

### 2. Wrong SIGWINCH Handler (main.c:30)
- SIGWINCH (window size change) calls cleanup_handler which exits the program
- **Fix**: Should call a resize handler instead

### 3. Terminal Attributes Too Restrictive (pty.c:103-110)
- Raw mode disables ECHO, ICANON which breaks bash line editing
- **Fix**: Use cooked mode or let bash handle terminal settings

### 4. Prompt Used Before Initialization (terminal.c:104)
- term->prompt is checked at line 104 but update_prompt() isn't called until line 146
- **Fix**: Move update_prompt() call before buffer initialization

### 5. Missing ANSI Escape Sequence Parser
- process_output() doesn't handle ANSI codes (colors, cursor movement, clear screen, etc.)
- **Impact**: Terminal will display raw escape codes instead of processing them
- **Fix**: Implement full VT100/ANSI escape sequence parser

## Moderate Issues

### 6. Font Loaded on Every Render Call (renderer.c:306-336)
- XLoadFont called 60 times per second
- **Fix**: Load font once in renderer_create(), cache the Font handle

### 7. Clipboard Blocks Indefinitely (clipboard.c:25)
- XNextEvent() waits forever if clipboard selection fails
- **Fix**: Use XCheckTypedWindowEvent with timeout

### 8. Autocomplete Never Called
- autocomplete_update() implemented but never invoked
- **Fix**: Call from key_callback when Tab is pressed

### 9. Autosuggest Never Called
- autosuggest_update() implemented but never invoked
- **Fix**: Call after each keystroke in normal mode

### 10. No History Persistence
- History stored in memory only, lost on exit
- **Fix**: Save/load from ~/.nierterm_history

### 11. Thread Safety Issues
- Terminal state accessed from input thread and main thread without locks
- **Fix**: Add mutex for shared state

### 12. GPU Rendering Disabled
- render_text_gpu() just calls CPU version
- **Fix**: Implement OpenGL text rendering or remove GPU claim

## Minor Issues

### 13. Excessive Debug Output
- fprintf(stderr, "[DEBUG]...") everywhere
- **Fix**: Add DEBUG macro, remove/conditionally compile debug statements

### 14. Hard-Coded Paths
- Font path: /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
- Shell path: /bin/bash
- **Fix**: Check multiple locations, use environment variables

### 15. No .gitignore
- Build artifacts (obj/, nierterm) not ignored
- **Fix**: Add .gitignore

### 16. Moonfly Colors Defined But Unused
- colors.h has full color scheme but DEFAULT_FG/BG_COLOR used instead
- **Fix**: Use moonfly colors or remove unused definitions

### 17. Inefficient Full-Screen Redraws
- XFillRectangle clears entire window every frame
- **Fix**: Use damage tracking, only redraw changed cells

### 18. No Ligature Support
- README claims ligature support but not implemented
- **Fix**: Implement via HarfBuzz or remove claim

### 19. No Nerd Font Support
- README claims nerd font support but no special handling
- **Fix**: Handle Unicode private use area or remove claim

### 20. Missing Dependencies Check in Makefile
- Doesn't verify FreeType, X11, etc. are installed
- **Fix**: Add configure script or better Makefile checks

### 21. No SIGCHLD Handler
- Shell exit not detected until read() returns 0
- **Fix**: Add SIGCHLD handler to detect early exit

### 22. Potential Buffer Overflows
- snprintf() return values not checked
- Several strcpy/strncpy calls
- **Fix**: Check return values, use safer string functions

### 23. XFreeFont Called on Font Not Allocated
- renderer.c:422 calls XFreeFont(font_struct) but should be separate call
- **Fix**: XUnloadFont(font) and XFreeFont(font_struct) separately

## Architecture Issues

### 24. No VT Emulator
- This is a terminal emulator but doesn't emulate a terminal
- Currently just displays raw output
- **Needs**: Full VT100/VT220/xterm emulation layer

### 25. Pane Master FD Never Set
- Pane structure has master_fd and pid fields but they're never populated
- **Fix**: Either remove fields or implement per-pane PTYs

### 26. SSH Integration Not Used
- ssh.c exists but never called from main code
- **Fix**: Add command-line flag or keybinding to open SSH connection

### 27. Fuzzy Finder Not Triggered Properly
- Ctrl+F triggers fuzzy_start() but suggestions won't display
- **Fix**: Add fuzzy finder UI overlay rendering

## Build System Issues

### 28. macOS Support Incomplete
- MACOS blocks exist but mostly empty
- **Fix**: Implement macOS versions or remove macOS claims

### 29. Cross-Compilation Instructions Incomplete
- BUILD.md mentions osxcross but no detailed steps
- **Fix**: Complete cross-compile documentation

## Documentation Issues

### 30. Features Listed But Not Implemented
- README claims: tmux integration, fuzzy finder, autocomplete, autosuggestions
- Reality: Autocomplete/autosuggest not integrated, fuzzy finder has no UI, tmux is just pane splitting
- **Fix**: Update README to reflect actual state or implement features

## Testing

### 31. No Tests
- No unit tests, integration tests, or test suite
- **Fix**: Add basic test infrastructure

## Recommendations

### Priority 1 (Must Fix for Basic Functionality):
1. Fix ANSI escape sequence parsing (Issue #5)
2. Fix memory leak (Issue #1)
3. Fix SIGWINCH handler (Issue #2)
4. Fix prompt initialization (Issue #4)
5. Fix font loading performance (Issue #6)

### Priority 2 (Needed for Claimed Features):
6. Integrate autocomplete (Issue #8)
7. Integrate autosuggest (Issue #9)
8. Add fuzzy finder UI (Issue #27)
9. Fix terminal attributes (Issue #3)

### Priority 3 (Polish):
10. Add .gitignore (Issue #15)
11. Remove debug output (Issue #13)
12. Fix thread safety (Issue #11)
13. Add history persistence (Issue #10)

### Priority 4 (Future):
14. Implement GPU rendering or remove claim
15. Add ligature support or remove claim
16. Complete macOS support or remove claim
17. Add comprehensive VT emulation
