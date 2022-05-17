FROM alpine:3.14.6 as builder
RUN apk add --no-cache build-base
ADD endlessh.c Makefile /
RUN make


FROM alpine:3.14.6

COPY --from=builder /endlessh /

EXPOSE 2222/tcp

ENTRYPOINT ["/endlessh"]

CMD ["-v"]
