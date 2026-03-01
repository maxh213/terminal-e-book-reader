# tread - Terminal EPUB Reader

## Context

Build a terminal-based EPUB reader from scratch in C. The repo is greenfield (only README.md and .gitignore exist). The reader should feel like using nano or vi, support theme customization, link following, TOC navigation, reading position persistence, and be distributable via the AUR (paru).

## Tech Stack

- **C11** with **ncursesw** (wide-char for UTF-8)
- **libzip** - extract EPUB contents (EPUB = ZIP archive)
- **libxml2** - parse XML/XHTML (container.xml, OPF, NCX/nav.xhtml, chapter content)
- **GNU Make** with pkg-config for build
- No external EPUB library - direct parsing for full control

## Project Structure

```
terminal-e-book-reader/
├── Makefile
├── README.md
├── LICENSE
├── PKGBUILD
├── tread.1                    # man page
├── src/
│   ├── main.c                 # entry point, arg parsing
│   ├── epub.c/h               # EPUB ZIP extraction + OPF/spine parsing
│   ├── renderer.c/h           # XHTML -> terminal text with links/attributes
│   ├── ui.c/h                 # ncurses UI, input loop, scrolling, status bar
│   ├── toc.c/h                # TOC parsing (NCX + nav.xhtml) and popup menu
│   ├── theme.c/h              # theme loading, color pair setup
│   ├── state.c/h              # reading position save/restore
│   ├── config.c/h             # INI config file parser
│   └── util.c/h               # string helpers, path utils, safe malloc
```

## Core Data Structures

```c
// Book - loaded EPUB
typedef struct {
    char *title, *author;
    int version;                 // EPUB 2 or 3
    char *opf_dir;               // base dir for resolving relative hrefs
    char *toc_href;              // path to NCX or nav.xhtml
    ManifestItem *manifest;      // array of {id, href, media_type}
    int manifest_count;
    int *spine;                  // indices into manifest[], reading order
    int spine_count;
    void *zip_archive;           // zip_t* handle, kept open
} Book;

// RenderResult - rendered chapter
typedef struct {
    RenderLine *lines;           // array of {text, attrs[], len}
    int line_count;
    Link *links;                 // array of {href, line/col range, id}
    int link_count;
    Anchor *anchors;             // array of {id, line} for jump targets
    int anchor_count;
} RenderResult;

// TOCEntry - table of contents item
typedef struct {
    char *label, *href;
    int depth;                   // nesting level
    int spine_index;             // resolved spine index
} TOCEntry;

// Theme - color configuration
typedef struct {
    char *name;
    short fg, bg;                // main text
    short status_fg, status_bg;  // status bar
    short link_fg, link_bg;      // link text
    short heading_fg, heading_bg;
    short highlight_fg, highlight_bg;  // search/selection
    short toc_fg, toc_bg, toc_sel_fg, toc_sel_bg;
} Theme;

// ReadingState - saved position
typedef struct {
    int spine_index;
    int scroll_line;
} ReadingState;
```

## Control Scheme

Two modes selectable via `--vi` flag or `mode = vi` in config.

### Nano-style (default)

| Action              | Keys                        |
|---------------------|-----------------------------|
| Scroll down/up      | Arrow keys, Enter           |
| Page down/up        | PgDn/Space, PgUp/Backspace  |
| Next/prev chapter   | Ctrl+N, Ctrl+P              |
| Open TOC            | Ctrl+T                      |
| Search              | Ctrl+W                      |
| Cycle links         | Tab / Shift+Tab             |
| Follow link         | Enter (in link mode)        |
| Go back             | Ctrl+B                      |
| Theme selector      | Ctrl+E                      |
| Quit                | Ctrl+X                      |
| Help                | Ctrl+G                      |

### Vi-style

| Action              | Keys                        |
|---------------------|-----------------------------|
| Scroll down/up      | j/k, arrows                 |
| Page down/up        | Ctrl+F/Space, Ctrl+B/PgUp   |
| Half page down/up   | Ctrl+D, Ctrl+U              |
| Next/prev chapter   | l/h, ]/[                    |
| Open TOC            | t                           |
| Search fwd/back     | /, ?                        |
| Search next/prev    | n, N                        |
| Cycle links         | Tab / Shift+Tab             |
| Follow link         | Enter                       |
| Go back             | Backspace                   |
| Theme selector      | T                           |
| Quit                | q, :q                       |

Input is mapped to abstract actions (ACTION_SCROLL_DOWN, etc.) so UI logic is keybinding-agnostic.

## Key Design Decisions

1. **ZIP stays open** - read chapters on demand from the archive rather than extracting to temp dir
2. **Lazy chapter rendering** - only current chapter is rendered; freed on chapter change
3. **htmlReadMemory** - use libxml2's HTML parser (tolerates non-strict XHTML in older EPUBs)
4. **Per-character attribute bitmask** - simplifies ncurses rendering loop
5. **INI config format** - trivial to parse (~60 lines of C), no extra dependency
6. **FNV-1a hash** for state filenames from absolute paths
7. **Navigation back stack** - circular buffer of 32 (spine_index, scroll_line) entries for link history

## Theme System

- Stored in `~/.config/tread/themes/*.conf` (INI format)
- Hardcoded default theme works without any config files
- Colors are ncurses color numbers (0-7 basic, 0-255 extended), `-1` for terminal default
- Theme selector popup lists available themes, applies immediately

## State Persistence

- Stored in `~/.local/share/tread/state/` (XDG compliant)
- Filename: FNV-1a hash of absolute path as 8 hex digits + `.state`
- Saves spine_index + scroll_line on quit, restores on reopen
- Validates stored filepath matches to handle hash collisions

## AUR Packaging

PKGBUILD with `depends=('libzip' 'libxml2' 'ncurses')`, `makedepends=('gcc' 'make' 'pkg-config')`. Makefile provides `install` target putting binary in `$(PREFIX)/bin`, man page in `share/man/man1`, license in `share/licenses`.

## Implementation Phases

### Phase 1: Skeleton + EPUB Parsing
- Project scaffolding: Makefile, directory structure, `main.c` with arg parsing
- `util.c/h`: xmalloc, xstrdup, path utilities
- `epub.c/h`: open ZIP, parse container.xml -> OPF -> manifest/spine/metadata
- **Test**: `tread book.epub` prints first chapter XHTML to stdout

### Phase 2: Basic Renderer + Terminal Display
- `renderer.c/h`: strip HTML tags, extract text, word-wrap to terminal width (plain text only)
- `ui.c/h`: ncurses init, render lines with scrolling, status bar, Ctrl+X to quit
- Chapter navigation (next/prev)
- **Test**: read and scroll through a book in the terminal

### Phase 3: Text Formatting + Links
- Enhance renderer: bold/italic/underline attributes, heading colors, `<a>` link tracking
- Enhance UI: apply ncurses attributes, link tab-cycling, link following, back stack

### Phase 4: Table of Contents
- `toc.c/h`: parse NCX (EPUB 2) and nav.xhtml (EPUB 3)
- TOC popup overlay in UI with selection and jump

### Phase 5: Config + Themes
- `config.c/h`: INI parser, load from `~/.config/tread/tread.conf`
- `theme.c/h`: load/apply themes, ship 2-3 built-in themes
- Theme selector popup
- Vi-mode keybindings (second keybinding table)

### Phase 6: State Persistence
- `state.c/h`: save/load reading position
- Wire into main.c (load on open, save on quit)

### Phase 7: Search
- Search prompt in UI, highlight matches, navigate between them

### Phase 8: Polish + Packaging
- Man page, README with usage docs
- PKGBUILD, test with `makepkg -si`
- Terminal resize handling (SIGWINCH / KEY_RESIZE)
- Edge cases: encrypted EPUBs (error msg), malformed XHTML (recovery mode)

## Verification

1. Build: `make` should compile cleanly with `-Wall -Wextra -pedantic`
2. Run: `./tread test.epub` - verify scrolling, chapter nav, formatting
3. TOC: Ctrl+T opens contents, selecting entry jumps correctly
4. Links: Tab cycles links, Enter follows, Ctrl+B goes back
5. State: quit and reopen same file, verify position restored
6. Themes: Ctrl+E opens selector, changing theme updates colors
7. AUR: `makepkg -si` installs successfully, `tread` available on PATH
