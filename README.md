# Endlessh: an SSH tarpit

Endlessh is an SSH tarpit [that *very* slowly sends an endless, random
SSH banner][np]. It keeps SSH clients locked up for hours or even days
at a time. The purpose is to put your real SSH server on another port
and then let the script kiddies get stuck in this tarpit instead of
bothering a real server.

Since the tarpit is in the banner before any cryptographic exchange
occurs, this program doesn't depend on any cryptographic libraries. It's
a simple, single-threaded, standalone C program. It uses `poll()` to
trap multiple clients at a time.

## Quick Usage 

Change the default ssh port before doing this and restart the real ssh service to run on another port!

```
sudo nano /etc/ssh/sshd_config
# Change Port 22 to something else within valid range
sudo systemctl restart sshd.service

# for debian / ubuntu distros
sudo apt install git -y 

# for arch distros
sudo pacman -S git

git clone https://github.com/skeeto/endlessh.git
cd endlessh
```
### Docker

```
sudo docker build .
sudo docker run -d --restart=always -p 22:2222 endlessh
```

### without Docker

```
sudo make install
sudo crontab -e
# insert:
@reboot sudo endlessh -p 22
# safe the file && sudo reboot
```

## Usage

Usage information is printed with `-h`.

```
Usage: endlessh [-vhs] [-d MS] [-f CONFIG] [-l LEN] [-m LIMIT] [-p PORT]
  -4        Bind to IPv4 only
  -6        Bind to IPv6 only
  -d INT    Message millisecond delay [10000]
  -f        Set and load config file [/etc/endlessh/config]
  -h        Print this help message and exit
  -l INT    Maximum banner line length (3-255) [32]
  -m INT    Maximum number of clients [4096]
  -p INT    Listening port [2222]
  -s        Print diagnostics to syslog instead of standard output
  -v        Print diagnostics (repeatable)
```

Argument order matters. The configuration file is loaded when the `-f`
argument is processed, so only the options that follow will override the
configuration file.

By default no log messages are produced. The first `-v` enables basic
logging and a second `-v` enables debugging logging (noisy). All log
messages are sent to standard output by default. `-s` causes them to be
sent to syslog.

    endlessh -v >endlessh.log 2>endlessh.err

A SIGTERM signal will gracefully shut down the daemon, allowing it to
write a complete, consistent log.

A SIGHUP signal requests a reload of the configuration file (`-f`).

A SIGUSR1 signal will print connections stats to the log.

## Sample Configuration File

The configuration file has similar syntax to OpenSSH.

```
# The port on which to listen for new SSH connections.
Port 2222

# The endless banner is sent one line at a time. This is the delay
# in milliseconds between individual lines.
Delay 10000

# The length of each line is randomized. This controls the maximum
# length of each line. Shorter lines may keep clients on for longer if
# they give up after a certain number of bytes.
MaxLineLength 32

# Maximum number of connections to accept at a time. Connections beyond
# this are not immediately rejected, but will wait in the queue.
MaxClients 4096

# Set the detail level for the log.
#   0 = Quiet
#   1 = Standard, useful log messages
#   2 = Very noisy debugging information
LogLevel 0

# Set the family of the listening socket
#   0 = Use IPv4 Mapped IPv6 (Both v4 and v6, default)
#   4 = Use IPv4 only
#   6 = Use IPv6 only
BindFamily 0
```

## Build issues

Some more esoteric systems require extra configuration when building.

### RHEL 6 / CentOS 6

This system uses a version of glibc older than 2.17 (December 2012), and
`clock_gettime(2)` is still in librt. For these systems you will need to
link against librt:

    make LDLIBS=-lrt

### Solaris / illumos

These systems don't include all the necessary functionality in libc and
the linker requires some extra libraries:

    make CC=gcc LDLIBS='-lnsl -lrt -lsocket'

If you're not using GCC or Clang, also override `CFLAGS` and `LDFLAGS`
to remove GCC-specific options. For example, on Solaris:

    make CFLAGS=-fast LDFLAGS= LDLIBS='-lnsl -lrt -lsocket'

The feature test macros on these systems isn't reliable, so you may also
need to use `-D__EXTENSIONS__` in `CFLAGS`.

### OpenBSD

The man page needs to go into a different path for OpenBSD's `man` command:

```
diff --git a/Makefile b/Makefile
index 119347a..dedf69d 100644
--- a/Makefile
+++ b/Makefile
@@ -14,8 +14,8 @@ endlessh: endlessh.c
 install: endlessh
        install -d $(DESTDIR)$(PREFIX)/bin
        install -m 755 endlessh $(DESTDIR)$(PREFIX)/bin/
-       install -d $(DESTDIR)$(PREFIX)/share/man/man1
-       install -m 644 endlessh.1 $(DESTDIR)$(PREFIX)/share/man/man1/
+       install -d $(DESTDIR)$(PREFIX)/man/man1
+       install -m 644 endlessh.1 $(DESTDIR)$(PREFIX)/man/man1/

 clean:
        rm -rf endlessh
```

[np]: https://nullprogram.com/blog/2019/03/22/
