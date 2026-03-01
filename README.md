# tread - Terminal EPUB Reader

A lightweight terminal-based EPUB reader built in C with ncurses. Supports text formatting, link following, table of contents navigation, theme customization, and reading position persistence.

## Features

- EPUB 2 and EPUB 3 support
- Text formatting: bold, italic, underline, headings
- Hyperlink following with back-navigation stack
- Table of contents popup (NCX and nav.xhtml)
- Configurable color themes (3 built-in + custom themes)
- Reading position saved/restored automatically
- Nano-style (default) and vi-style keybindings
- Search with match highlighting
- Terminal resize handling

## Dependencies

- `libzip` - EPUB archive extraction
- `libxml2` - XML/XHTML parsing
- `ncurses` (ncursesw) - terminal UI

## Building

```sh
make
```

## Installation

```sh
sudo make install
# or with custom prefix:
make PREFIX=~/.local install
```

### Arch Linux (AUR)

```sh
makepkg -si
```

## Usage

```sh
tread book.epub
tread --vi book.epub        # vi-style keybindings
tread --theme dark book.epub # select theme
```

## Keybindings

### Nano-style (default)

| Action            | Keys                       |
|-------------------|----------------------------|
| Scroll down/up    | Arrow keys, Enter          |
| Page down/up      | PgDn/Space, PgUp/Backspace |
| Next/prev chapter | Ctrl+N, Ctrl+P             |
| Table of contents | Ctrl+T                     |
| Search            | Ctrl+W                     |
| Cycle links       | Tab / Shift+Tab            |
| Follow link       | Enter (when link selected) |
| Go back           | Ctrl+B                     |
| Theme selector    | Ctrl+E                     |
| Help              | Ctrl+G                     |
| Quit              | Ctrl+X                     |

### Vi-style (`--vi` or `mode = vi` in config)

| Action            | Keys                       |
|-------------------|----------------------------|
| Scroll down/up    | j/k, arrows                |
| Page down/up      | Ctrl+F/Space, Ctrl+B/PgUp  |
| Half page         | Ctrl+D, Ctrl+U             |
| Next/prev chapter | l/h, ]/[                   |
| Table of contents | t                          |
| Search fwd/back   | /, ?                       |
| Search next/prev  | n, N                       |
| Cycle links       | Tab / Shift+Tab            |
| Follow link       | Enter                      |
| Go back           | Backspace                  |
| Theme selector    | T                          |
| Quit              | q                          |

## Configuration

Config file: `~/.config/tread/tread.conf`

```ini
mode = vi
theme = dark
```

## Custom Themes

Place `.conf` files in `~/.config/tread/themes/`:

```ini
name = solarized
fg = 12
bg = 8
status_fg = 0
status_bg = 3
link_fg = 4
link_bg = -1
heading_fg = 3
heading_bg = -1
```

Colors are ncurses color numbers (0-7 basic, 0-255 extended), `-1` for terminal default.

## License

MIT
