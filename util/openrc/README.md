# Running `endlessh` on OpenRC

OpenRC is used by Gentoo and Alpine.

Add the provided service to `/etc/init.d`, enable and start it.

```sh
rc-update add endlessh
rc-service endlessh start
```
