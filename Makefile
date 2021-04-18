.POSIX:
CC       = cc
WARNINGS = -Wall -Wcast-align -Wcast-qual -Wextra -Wfloat-equal -Wformat=2 -Winit-self -Wmissing-format-attribute -Wmissing-noreturn -Wmissing-prototypes -Wnull-dereference -Wpointer-arith -Wshadow -Wstrict-prototypes -Wundef -Wunused -Wwrite-strings
CFLAGS   = -std=c99 $(WARNINGS) -Os
CPPFLAGS =
LDFLAGS  = -ggdb3
LDLIBS   =
PREFIX   = /usr/local

all: endlessh

endlessh: endlessh.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ endlessh.c $(LDLIBS)

install: endlessh
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 endlessh $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 644 endlessh.1 $(DESTDIR)$(PREFIX)/share/man/man1/

clean:
	rm -rf endlessh
