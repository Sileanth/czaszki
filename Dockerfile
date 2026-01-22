FROM alpine:latest AS builder

RUN apk add --no-cache gcc musl-dev make

WORKDIR /build
COPY chess_engine.c Makefile ./
RUN make CFLAGS="-O3 -fPIC" all


FROM alpine:latest

RUN apk add --no-cache python3 py3-pip
COPY --from=ghcr.io/astral-sh/uv:latest /uv /usr/local/bin/uv

WORKDIR /app

COPY pyproject.toml ./
RUN uv sync --no-dev --frozen || uv sync --no-dev

COPY --from=builder /build/libchess.so ./
COPY main.py engine_wrapper.py http_server.py ./
COPY templates/ ./templates/

EXPOSE 8080

ENTRYPOINT ["uv", "run", "main.py"]
