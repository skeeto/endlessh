# Running `endlessh` on OpenBSD

## Covering IPv4 and IPv6

If you want to cover both IPv4 and IPv6 you'll need to run *two* instances of
`endlessh` due to OpenBSD limitations. Here's how I did it:

- copy the `endlessh` script to `rc.d` twice, as `endlessh` and `endlessh6`
- copy the `config` file to `/etc/endlessh` twice, as `config` and `config6`
  - use `BindFamily 4` in `config`
  - use `BindFamily 6` in `config6`
- in `rc.conf.local` force `endlessh6` to load `config6` like so:

```
endlessh6_flags=-s -f /etc/endlessh/config6
endlessh_flags=-s
```

## Covering more than 128 connections

The defaults in OpenBSD only allow for 128 open file descriptors per process,
so regardless of the `MaxClients` setting in `/etc/config` you'll end up with
something like 124 clients at the most.
You can increase these limits in `/etc/login.conf` for `endlessh` (and
`endlessh6`) like so:

```
endlessh:\
	:openfiles=1024:\
	:tc=daemon:
endlessh6:\
	:openfiles=1024:\
	:tc=daemon:
```
