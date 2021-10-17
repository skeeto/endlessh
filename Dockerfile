FROM alpine:3.9 as builder

RUN apk add --no-cache make gcc libc-dev
ADD endlessh.c Makefile /
RUN make LDFLAGS="-static -s"

FROM scratch

COPY --from=builder /endlessh /
EXPOSE 2222/tcp
ENTRYPOINT ["/endlessh"]
CMD ["-v"]
