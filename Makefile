.POSIX:
CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -Wno-missing-field-initializers -xO4 -D_XPG6 -D__EXTENSIONS__=1
LDFLAGS =
LDLIBS  = -lsocket -lnsl -lrt

endlessh: endlessh.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ endlessh.c $(LDLIBS)

clean:
	rm -rf endlessh
