FROM gliderlabs/alpine:3.4
RUN \
      apk add --no-cache make gcc git libc-dev && \
      git clone https://github.com/skeeto/endlessh && \
      cd endlessh && \
      make
EXPOSE 2222/tcp
ENTRYPOINT /endlessh/endlessh
