# HTTP proxy in C

An educational HTTP GET proxy implemented with POSIX sockets and a pthread worker pool. Written as a systems-programming exercise, it demonstrates request parsing, concurrent dispatch, filtering and filesystem caching.

## Implemented behavior

- Listens on a configurable port and dispatches accepted connections to a fixed-size thread pool.
- Accepts HTTP/1.0 and HTTP/1.1 GET requests and connects to the origin on port 80.
- Filters hostnames and IPv4 addresses/subnets from a text file.
- Stores responses in a local filesystem cache and serves cached content on subsequent requests.
- Stops accepting connections after the configured request count and drains the worker pool.

## Build on Linux

Requires a C99 compiler, pthreads and CMake 3.16+.

```sh
cmake -S . -B build
cmake --build build
```

## Run in an isolated local environment

```sh
mkdir -p sandbox
cd sandbox
printf 'blocked.example\n192.0.2.0/24\n' > filters.txt
../build/Proxy_Server 8080 4 20 filters.txt
```

Arguments are `<port> <pool-size> <max-number-of-request> <filter-file>`. The binary is named `Proxy_Server` by CMake, even though the program's usage message uses `proxyServer`.

In another terminal, a filtering example is:

```sh
curl --noproxy '' --proxy http://127.0.0.1:8080 http://192.0.2.1/
```

The subnet rule is intended to produce `403 Forbidden` without forwarding to the origin. Run only in a disposable VM/container or otherwise isolated environment: the current listener binds to all interfaces, and cached paths are derived from requests.

## Source map

- `proxyServer.c`: request handling, filtering, upstream connection and cache I/O.
- `threadpool.c` / `threadpool.h`: worker queue, synchronization and shutdown.
- `CMakeLists.txt`: build definition.

## Limitations

This is **not a production proxy**. It does not support HTTPS CONNECT, authentication, cache expiry or complete HTTP semantics. The parser, cache paths and buffer handling need hardening; the source includes a potential out-of-bounds terminator write when a read fills the request buffer. No automated test suite is included.

Good follow-up work: sanitizer-backed parsing tests, bounded headers and timeouts, safe cache-path handling, and tests for concurrent requests and shutdown.
