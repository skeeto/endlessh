# Endlessh: an SSH tarpit

Endlessh is an SSH tarpit that *very* slowly sends an endless, random
SSH banner. It keeps SSH clients locked up for hours or even days at a
time. The purpose is to put your real SSH server on another port and
then let the script kiddies get stuck in this tarpit instead of
bothering a real server.

Since the tarpit is in the banner before any cryptographic exchange
occurs, this program doesn't depend on any cryptographic libraries. It's
a simple, single-threaded, standalone C program. It uses `poll()` to
trap multiple clients at a time.

## Usage

Usage information is printed with `-h`.

```
Usage: endlessh [-vh] [-d MS] [-f CONFIG] [-l LEN] [-m LIMIT] [-p PORT]
  -d INT    Message millisecond delay [10000]
  -f        Set and load config file [/etc/endlessh/config]
  -h        Print this help message and exit
  -l INT    Maximum banner line length (3-255) [32]
  -m INT    Maximum number of clients [4096]
  -p INT    Listening port [2222]
  -v        Print diagnostics to standard output (repeatable)
```

Argument order matters. The configuration file is loaded when the `-f`
argument is processed, so only the options that follow will override the
configuration file.

By default no log messages are produced. The first `-v` enables basic
logging and a second `-v` enables debugging logging (noisy). All log
messages are sent to standard output.

    endlessh -v >endlessh.log 2>endlessh.err

A SIGTERM signal will gracefully shut down the daemon, allowing it to
write a complete, consistent log.

A SIGHUP signal requests a reload of the configuration file (`-f`).

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
```

## Build issues

RHEL 6 and CentOS 6 use a version of glibc older than 2.17 (December
2012), and `clock_gettime(2)` is still in librt. For these systems you
will need to link against librt:

    make LDLIBS=-lrt
