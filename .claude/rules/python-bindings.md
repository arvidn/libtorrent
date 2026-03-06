---
paths:
  - "bindings/python/**"
---

## Python Bindings

Source in `bindings/python/src/*.cpp`; one `.cpp` file per exposed subsystem (e.g. `session.cpp`, `torrent_handle.cpp`, `alert.cpp`). Built as a Python extension module (`libtorrent.so`) using boost.python.

**Building:**
```sh
cd bindings/python && b2
```
CMake also builds them when `-Dpython-bindings=ON` is passed.

**Testing:**

Tests live in `bindings/python/tests/*_test.py` and are run with pytest:
```sh
cd bindings/python && python -m pytest tests/
```
There is a roughly one-to-one correspondence between `src/foo.cpp` and `tests/foo_test.py`. Tests must be isolated — no real network access, no persistent filesystem side-effects. Use `get_isolated_settings()` from `tests/lib.py` when constructing a session, and `tempfile.TemporaryDirectory()` for any disk I/O. See `bindings/python/tests/guidelines.txt` for the full testing conventions.

**Type stubs:**

The type stub file is `bindings/python/libtorrent/__init__.pyi`. It was originally bootstrapped with `stubgen` but is now **maintained entirely by hand** — do not regenerate it with stubgen, as that would lose the carefully written annotations. When adding a new binding, add the corresponding type annotation to this file manually. The header comment in the file documents the conventions used (e.g. how boost.python enums, positional-only args, and the `_BoostBaseClass` metaclass are represented).

**Design philosophy — classes over dicts:**

Prefer exposing real C++ classes via boost.python rather than converting them to plain Python dicts. Earlier versions of the bindings returned plain `dict` objects for things like peer info and torrent status; these have been replaced with proper bound classes (`peer_info`, `torrent_status`, etc.) that mirror the C++ types. When adding new bindings, follow this pattern: expose the C++ struct/class directly rather than unpacking it into a dict.

Note: boost.python strictly requires `dict` and `list` (not duck-typed `Mapping`/`Sequence` abstractions) for function arguments that cross the C++ boundary. This is a boost.python limitation and not a design choice; the type stubs reflect this with `dict` and `list` rather than `Mapping` or `Sequence`.
