# Minimal HTTP Server in C (Ring Buffer + State Machine)

This project is a **minimal HTTP/1.1 server written in C**, built to demonstrate how HTTP works **on top of TCP**, without hiding details behind libraries.

The focus is on:
- TCP stream semantics (`recv`)
- Ring buffer for incremental input
- Explicit state-machine HTTP parsing
- Clear separation between I/O, framing, and parsing
- Dockerized runtime for reproducibility

This is an educational / systems-level project, not a production web server.

---

## Features

- TCP server using BSD sockets
- Ring buffer for handling partial reads
- HTTP request parsing via explicit state machine
  - `ST_HEADER` → parse headers
  - `ST_BODY` → parse body (based on `Content-Length`)
- Correct handling of TCP stream boundaries
- One request → one response (`Connection: close`)
- Dockerized using multi-stage build
- Makefile for common development workflows

---

## Project Structure

.
├── src/
│ └── main.c # HTTP server implementation
├── Dockerfile # Multi-stage Docker build
├── Makefile # Build / run / stop helpers
├── .dockerignore
└── README.md

| Command        | Description                         |
| -------------- | ----------------------------------- |
| `make build`   | Build Docker image                  |
| `make run`     | Run container (foreground)          |
| `make stop`    | Stop running container              |
| `make restart` | Stop + run                          |
| `make clean`   | Remove Docker image                 |
| `make logs`    | Follow container logs               |
| `make exec`    | Enter running container (`/bin/sh`) |

