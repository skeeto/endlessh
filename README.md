# Endlessh: an SSH tarpit

Endlessh is an SSH tarpit that *very* slowly sends an endless, random
SSH banner. It keeps SSH clients locked up for hours or even days at at
time. The purpose is to put your real SSH server on another port and
then let the script kiddies get themselves stuck in this tarpit instead
of bothering a real server.

Since the tarpit is the banner, before any cryptographic exchange
occurs, this program doesn't depend on any cryptographic libraries. It's
a simple, single-threaded, standalone C program. It uses `poll()` to
trap multiple clients at a time.

### Usage

Usage information is printed with `-h`.

```
Usage: endlessh [-vh] [-d MSECS] [-m LIMIT] [-p PORT]
  -d INT    Message millisecond delay [10000]
  -h        Print this help message and exit
  -m INT    Maximum number of clients [4096]
  -p INT    Listening port [2222]
  -v        Print diagnostics to standard output (repeatable)
```

By default no log messages are produced. The first `-v` enables basic
logging and a second `-v` enables debug logging (noisy). All log
messages are sent to standard output.

    endlessh -v >endlessh.log 2>endlessh.err

The purpose of limiting the number of clients (`-m`) is to avoid tying
up too many system resources with the tarpit. Clients beyond this limit
are left in the accept queue, not rejected instantly.

A SIGTERM signal will gracefully shut down the daemon, allowing it to
write a complete, consistent log.
