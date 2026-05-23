# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

libtorrent-rasterbar is a C++17 BitTorrent library (version 2.1.0). It supports v1 and v2 torrents, DHT, WebTorrent (WebRTC), and optional I2P/SSL.

## Build Systems

The project has **two build systems**: boost-build (b2/bjam) and CMake. **Boost-build is the preferred build system** per the contributing guidelines.

### Boost-build (b2) - preferred

**Never add `-j` (parallel jobs) flags to b2 commands.** b2 manages parallelism on its own.

Build the library (from repo root):
```sh
b2
```

Run unit tests:
```sh
cd test && b2
```

Run a single test:
```sh
cd test && b2 test_session
```

Run deterministic tests (no flaky networking):
```sh
cd test && b2 deterministic-tests
```

Run simulations:
```sh
cd simulation && b2
```

Build with developer options (typical for development):
```sh
cd test && b2 asserts=on invariant-checks=full debug-iterators=on picker-debugging=on
```

Build with sanitizers:
```sh
cd test && b2 address-sanitizer=norecover undefined-sanitizer=norecover asserts=on
```

Build examples, tools, fuzzers, python bindings:
```sh
cd examples && b2
cd tools && b2
cd fuzzers && b2 clang
cd bindings/python && b2
```

### CMake — alternative

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -Dbuild_tests=ON
cmake --build build
cd build && ctest
```

Run a single test via CMake:
```sh
cd build && ./test/test_session
```

## Pre-commit

The repo uses [pre-commit](https://pre-commit.com). Before pushing:
```sh
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

Pre-commit hooks include trailing whitespace, YAML/TOML/XML checks, RST formatting, Python formatting (black, isort, flake8, mypy, autoflake), and auto-generation of:
- `include/libtorrent/fwd.hpp` and `include/libtorrent/libtorrent.hpp` via `tools/gen_fwd.py` and `tools/gen_convenience_header.py`
- C binding headers (`bindings/c/include/libtorrent_settings.h`, `libtorrent_alerts.h`) via `bindings/c/tools/gen_header.py` and `bindings/c/tools/gen_alert_header.py`

The C++ format check (`git-clang-format-18`) is the only hook that requires a
system install -- `apt install clang-format-18` (or platform equivalent). It
formats only the lines that differ from HEAD, not whole files; the codebase is
being migrated to the `.clang-format` style incrementally rather than via a
flag-day reformat.

## Key Architecture

### Threading Model

Three thread categories:
1. **Main network thread** — all sockets, session/torrent/peer state, boost.asio event loop
2. **Disk I/O thread(s)** — reads, writes, and SHA-1/SHA-256 piece verification (count controlled by `settings_pack::aio_threads`)
3. **Resolver thread** — spawned by boost.asio for async DNS on platforms without native async getaddrinfo

All interaction with session/torrent state from outside must go through the network thread (via `session_handle` and `torrent_handle`).

### Core Classes

- **`session`** (`include/libtorrent/session.hpp`) — public API, pimpl over `session_impl`
- **`session_impl`** (`include/libtorrent/aux_/session_impl.hpp`) — all session state: torrent list, connection list, global rate limits, DHT state, port mapping
- **`torrent`** (`include/libtorrent/aux_/torrent.hpp`, `src/torrent.cpp`) — all state for a single swarm: piece picker, peer connections, file storage
- **`torrent_handle`** (`include/libtorrent/torrent_handle.hpp`) — public pimpl handle, weak reference to `torrent`; sends messages to network thread
- **`peer_connection`** / **`bt_peer_connection`** — BitTorrent protocol implementation
- **`piece_picker`** — download strategy (which blocks to request from which peers).  See `.claude/rules/piece-picker.md` for a detailed description.
- **`peer_list`** — list of known (not necessarily connected) peers for a swarm

### Disk I/O

Three disk backends:
- `mmap_disk_io` — mmap-based (default on 64-bit when mmap is available)
- `posix_disk_io` — fallback single-threaded POSIX I/O (used on 32-bit or without mmap)
- `pread_disk_io` — multi-threaded backend using `pread()`/`pwrite()`. See `.claude/rules/disk-cache.md` for a detailed description.

### DHT (Kademlia)

Source in `src/kademlia/`, headers in `include/libtorrent/kademlia/`. Includes ed25519 signing (`src/ed25519/`).

### Extensions

Protocol extensions live in `src/` and `include/libtorrent/extensions/`:
- `ut_metadata` — magnet link metadata exchange
- `ut_pex` — peer exchange
- `smart_ban` — ban peers that send corrupt data
- `i2p_pex` — peer exchange for I2P peers

### WebTorrent (WebRTC)

Optional feature controlled by `webtorrent=on` / `-Dwebtorrent=ON`. Uses `deps/libdatachannel` and the `rtc_stream`/`rtc_signaling` subsystem.

### Python Bindings

Source in `bindings/python/src/*.cpp`, built as `libtorrent.so` using boost.python. See `.claude/rules/python-bindings.md` for detailed conventions.

### Simulations

`simulation/` contains network simulation tests that use `libsimulator` (a deterministic virtual network). These test high-level behaviors (swarms, DHT, session management) without real network access. All simulation tests run with `invariant-checks=full`, `asserts=on`, `debug-iterators=on`.

### Terminology

- **piece** — SHA-1/SHA-256 verified chunk of torrent data (typically power-of-two size)
- **block** — 16 KiB sub-unit of a piece (the transfer unit in the protocol)
- **torrent_peer** — a known-but-not-connected peer entry
- **peer_connection** — an active connection to a peer

## Key Build Options (b2 features)

| Feature | Values |
|---------|--------|
| `crypto` | `built-in`, `openssl`, `openssl-shared`, `wolfssl`, `gcrypt`, `gnutls`, `libcrypto` |
| `asserts` | `on`, `off`, `production`, `system` |
| `invariant-checks` | `off`, `on`, `full` |
| `logging` | `on`, `off` |
| `deprecated-functions` | `on`, `off`, `1`, `2`, `3`, `4` |
| `webtorrent` | `on`, `off` |
| `address-sanitizer` | `norecover`, `recover`, `off` |
| `undefined-sanitizer` | `norecover`, `recover`, `off` |
| `thread-sanitizer` | `norecover`, `recover`, `off` |
| `picker-debugging` | `on`, `off` |
| `debug-iterators` | `default`, `off`, `on`, `harden` |

`deprecated-functions` selects the ABI/API version via `TORRENT_ABI_VERSION`. Different values are **link-incompatible**. Higher numbers expose a more modern interface and remove older deprecated APIs:

| Value | `TORRENT_ABI_VERSION` | Corresponds to |
|-------|-----------------------|----------------|
| `on`  | (oldest supported)    | libtorrent 1.x |
| `1`   | 1                     | libtorrent 1.x |
| `2`   | 2                     | libtorrent 2.0 |
| `3`   | 3                     | libtorrent 2.x |
| `4`   | 4                     | libtorrent 2.1 |
| `off` | 100 (newest)          | latest API     |

## Directory Layout

```
src/                    main library source
src/kademlia/           DHT implementation
src/ed25519/            ed25519 signing (vendored)
include/libtorrent/     public API headers
include/libtorrent/aux_/  internal headers (not public API)
include/libtorrent/kademlia/  DHT headers
include/libtorrent/extensions/  extension headers
test/                   unit tests (test_*.cpp)
simulation/             network simulation tests (test_*.cpp)
fuzzers/src/            fuzz targets
examples/               example programs
tools/                  utility scripts and programs
bindings/python/        Python bindings (boost.python)
bindings/c/             C API bindings
deps/                   vendored dependencies
  deps/try_signal/      signal handling
  deps/asio-gnutls/     GnuTLS adapter for asio
  deps/json/            nlohmann/json (for WebTorrent)
  deps/libdatachannel/  WebRTC (for WebTorrent)
```

## Adding New Source Files

When adding a new `.cpp` or `.hpp` file, it must be added to **all three** build systems:
1. `Jamfile` (boost-build)
2. `CMakeLists.txt`
3. `Makefile` (if applicable)

## Code-Generation Tools

After modifying public API headers, regenerate the derived headers by running these scripts from the repo root:

- `tools/gen_fwd.py` — regenerates `include/libtorrent/fwd.hpp` (forward declarations for all public types)
- `tools/gen_convenience_header.py` — regenerates `include/libtorrent/libtorrent.hpp` (the convenience header that includes all public headers)

These are also run automatically by the pre-commit hooks.

## Coding Conventions

- Comments must use ASCII characters only (no Unicode, smart quotes, em-dashes, etc.)
- Use a single space after a period in comments (not two spaces)
- Declare variables `const` whenever they are not reassigned after initialization
- C++17 throughout; no C++20 features yet
- The code must build with standard C++ and must not require compiler language extensions; optional extensions (e.g. `__builtin_*`, platform-specific pragmas) may be used when guarded by feature detection
- `namespace lt = libtorrent` alias is always available
- Asserts: use `TORRENT_ASSERT(cond)` (active when `TORRENT_USE_ASSERTS` is defined, i.e. debug builds)
- Invariant checks: expensive checks inside `#if TORRENT_USE_INVARIANT_CHECKS`
- Public API headers use `TORRENT_EXPORT` macro; internal symbols use hidden visibility
- Internal functions and classes use `TORRENT_EXPORT_EXTRA` macro; to grant access to tests
- ABI versioning via `TORRENT_VERSION_NAMESPACE_2/3/4` inline namespace macros (defined in `include/libtorrent/aux_/export.hpp`); `_2` = v1.2, `_3` = v2, `_4` = v2.1
- Warnings are treated as errors in CI (both gcc and clang)
- Changes to ABI (fields/ordering of public classes) must target `master`, not `RC_*` stable branches
- `settings_pack` enum values must be appended at the end of each enum group (int, bool, string) — never inserted in the middle — to avoid changing the numeric values of existing settings and breaking ABI
- prefer the C++ counterparts to C headers
- do not use using-statements in header files
- prefer to fully qualify standard types and functions
- prefer the short lt namespace alias when fully qualifying libtorrent types
- prefer using default member initializers over initializer list

### Strong Types

Avoid raw `int` for indices and flags; use `aux::strong_typedef` (`include/libtorrent/units.hpp`) and `flags::bitfield_flag` (`include/libtorrent/flags.hpp`). See `.claude/rules/strong-types.md` for details.
