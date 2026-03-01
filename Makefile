PREFIX   ?= /usr/local
BINDIR    = $(PREFIX)/bin
MANDIR    = $(PREFIX)/share/man/man1
LICENSEDIR= $(PREFIX)/share/licenses/tread

CC       ?= gcc
CFLAGS   += -std=c11 -Wall -Wextra -pedantic -D_GNU_SOURCE
CFLAGS   += $(shell pkg-config --cflags libzip libxml-2.0 ncursesw)
LDFLAGS  += $(shell pkg-config --libs libzip libxml-2.0 ncursesw)

SRC       = $(wildcard src/*.c)
OBJ       = $(SRC:.c=.o)
BIN       = tread

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 tread.1 $(DESTDIR)$(MANDIR)/tread.1
	install -Dm644 LICENSE $(DESTDIR)$(LICENSEDIR)/LICENSE

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(MANDIR)/tread.1
	rm -rf $(DESTDIR)$(LICENSEDIR)

.PHONY: all clean install uninstall
