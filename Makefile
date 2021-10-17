.POSIX:
CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wno-missing-field-initializers -Os
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

docker:
	docker build . -t endlessh
run:
	docker run --rm -it --init endlessh
