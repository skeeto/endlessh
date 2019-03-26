.POSIX:
CC      ?= cc
CFLAGS  ?= -Os
CFLAGS  += -std=c99 -Wall -Wextra -Wno-missing-field-initializers
LDFLAGS ?= -ggdb3
LDLIBS  ?=

endlessh: endlessh.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ endlessh.c $(LDLIBS)

clean:
	rm -rf endlessh
