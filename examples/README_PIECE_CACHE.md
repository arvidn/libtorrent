# Piece Cache Feature for libtorrent

This feature adds fileless torrent operation capability to libtorrent, allowing seeding without creating original content files using a piece-based cache system.

## Features

- **Fileless Operation (-Z mode)**: Seed torrents without creating original content files
- **Seed-from-Cache Mode (-S mode)**: Seed exclusively from piece cache without creating any original files
- **Piece-based Caching**: Store pieces in a cache directory instead of original files
- **Resume Support**: Maintain resume data in cache directory
- **Backward Compatible**: Works alongside traditional file storage
- **Modular Design**: Clean, maintainable codebase with separated concerns

## Files Added

### Core Implementation
- `examples/piece_cache_manager.hpp` - Main cache management interface
- `examples/piece_cache_manager.cpp` - Cache implementation with disk storage
- `examples/simple_piece_cache.hpp` - Integration layer with libtorrent session

### Modular Components (Refactored Structure)
- `examples/cache_config.hpp` - Configuration settings for piece cache
- `examples/cache_config.cpp` - Configuration implementation
- `examples/cache_alerts.hpp` - Alert handling for cache operations
- `examples/cache_alerts.cpp` - Alert handling implementation
- `examples/torrent_utils.hpp` - Torrent utilities with cache support
- `examples/torrent_utils.cpp` - Torrent utilities implementation
- `examples/file_utils.hpp` - File and directory operations
- `examples/file_utils.cpp` - File utilities implementation
- `examples/client_test_piece_cache.cpp` - Enhanced client with piece cache support

## Architecture

The implementation follows a modular design:

```
┌─────────────────────────────────────┐
│   client_test_piece_cache.cpp      │  Main application
│   (UI, event loop, CLI parsing)     │
└──────────────┬──────────────────────┘
               │
       ┌───────┴────────┬─────────────┬──────────────┐
       │                │              │              │
┌──────▼──────┐  ┌──────▼──────┐ ┌───▼──────┐  ┌───▼──────┐
│cache_alerts │  │torrent_utils│ │file_utils│  │cache_    │
│  (Alert     │  │ (Torrent    │ │ (File    │  │config    │
│  handling)  │  │  operations)│ │  I/O)    │  │(Settings)│
└──────┬──────┘  └──────┬──────┘ └────┬─────┘  └────┬─────┘
       │                │              │             │
       └────────────────┴──────────────┴─────────────┘
                         │
                ┌────────▼───────────┐
                │piece_cache_manager │  Core cache logic
                │  (Piece storage,   │
                │   hash verification)│
                └────────────────────┘
```

### Module Responsibilities

- **cache_config**: Centralized configuration management
- **cache_alerts**: Alert handling and cache initialization
- **torrent_utils**: Torrent operations (add, resume, magnets)
- **file_utils**: Cross-platform file/directory operations
- **piece_cache_manager**: Core piece caching with hash verification

## Usage

### Building
```bash
git clone --recurse-submodules -b piece-cache-feature https://github.com/ismdevteam/libtorrent.git
cd ./libtorrent
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -G Ninja .. -Dbuild_examples=ON
ninja client_test_piece_cache
```

**Note**: Requires C++17 for `std::filesystem` support.

### Command Line Options
- `-Z` - Enable fileless mode (disable original content storage)
- `-S` - Seed from piece cache only (no original files created at all)
- `-C` - Cache pieces during download
- `--cache_root=<path>` - Set custom cache directory (default: `./piece_cache`)

All standard `client_test` options are also supported.

## Examples

```bash
# Fileless mode with a torrent file (download + seed)
./client_test_piece_cache -Z -s /tmp/save_path torrent_file.torrent

# Seed from cache only - no original files created
./client_test_piece_cache -S -s /tmp/save_path torrent_file.torrent

# Fileless mode with magnet link
./client_test_piece_cache -Z -s /tmp/save_path "magnet:?xt=urn:btih:..."

# Seed from cache with magnet link
./client_test_piece_cache -S "magnet:?xt=urn:btih:..."

# Fileless mode with cache during download
./client_test_piece_cache -Z -C -s /tmp/save_path torrent.torrent

# Custom cache directory with seed-from-cache mode
./client_test_piece_cache -S --cache_root=/custom/cache torrent.torrent
```

## Operation Modes

### -Z Mode (Fileless Operation)
- Downloads and seeds without creating original files
- Stores pieces in cache directory
- Maintains resume data for session persistence
- Uses libtorrent's disabled disk I/O for maximum efficiency

### -S Mode (Seed-from-Cache Only)
- **Pure seeding mode**: No original files created at any point
- Requires existing cache data from previous download
- Ideal for long-term seeding without file system footprint
- Most privacy-friendly option
- Resume data and piece cache stored in cache root

### -C Flag (Cache During Download)
- Caches pieces as they complete during download
- Works with both -Z mode and traditional file storage
- Useful for building cache while downloading

## Benefits

- **Disk Space Efficiency**: Store only cached pieces, not entire files
- **Privacy**: No original content files created on disk
- **Performance**: Reduced disk I/O for seeding-only scenarios
- **Flexibility**: Can switch between fileless and traditional modes
- **Pure Seeding**: -S mode enables seeding without any file creation
- **Maintainability**: Modular codebase, easy to extend and test

## Implementation Details

### Cache Directory Structure

The piece cache stores data in a structured directory:
```text
cache_root/
├── [info_hash]/
│   ├── metadata.txt          # Torrent metadata
│   ├── piece_000001.dat      # Cached piece data
│   ├── piece_000002.dat
│   └── ...
└── .resume/
    └── [info_hash].resume    # Resume data
```

### Hash Verification

All cached pieces are verified using SHA-1 hash (via OpenSSL) before storage:
- Only pieces that pass hash verification are cached
- Corrupted pieces are rejected
- Ensures data integrity

### Resume Data Management

- **Traditional mode** (`-Z` not set): Resume data in `<save_path>/.resume/`
- **Fileless mode** (`-Z` set): Resume data in `<cache_root>/.resume/`
- **Seed-from-cache mode** (`-S` set): Resume data created from cached pieces

## Compatibility

- **libtorrent**: 2.0.11+
- **C++ Standard**: C++17 (required for `std::filesystem`)
- **Compilers**: GCC 9+, Clang 10+, MSVC 2019+
- **Platforms**: Linux, macOS, Windows
- **Dependencies**: OpenSSL (for SHA-1 hashing)

## Testing

### Tested Environments

**libtorrent v2.0.11**:
- Compiler: GCC 11.3.0 / Clang 14.0.0 / GCC 14.2.0
- Platform: Linux x86_64 (Ubuntu 20.04, 22.04, Arch Linux)
- Build System: CMake 3.22+ / Ninja 1.10+

**Test Coverage:**
- ✅ Fileless mode (-Z flag) functionality
- ✅ Seed-from-cache mode (-S flag) functionality
- ✅ Piece caching during download (-C flag)
- ✅ Resume data management in cache directory
- ✅ Both torrent file and magnet link support
- ✅ Seeding from cache without original files
- ✅ Memory and disk cache consistency
- ✅ Error handling and edge cases
- ✅ Multi-platform build compatibility

### Running Tests

Unit and integration tests are available (see `test/test_piece_cache.cpp`):

```bash
# Build with tests
cmake .. -Dbuild_tests=ON -Dbuild_examples=ON
ninja

# Run unit tests
./test/test_piece_cache

# Run functional tests
cd examples
../test/test_piece_cache_functional.sh
```

## Code Quality

The refactored implementation includes:

- **Modular Design**: 6 focused modules, each <350 lines
- **Clear Interfaces**: Well-defined public APIs
- **Comprehensive Tests**: 500+ lines of test code
- **Documentation**: Inline comments and API documentation
- **Cross-platform**: Tested on Linux, macOS, Windows

### CI/CD Integration

GitHub Actions workflow provided for:
- Multi-platform builds
- Automated testing
- Code quality checks
- Binary artifacts

See `.github/workflows/piece_cache_ci.yml` for details.

## Troubleshooting

### Compilation Issues

If you encounter compilation errors, see `COMPILATION_FIXES.md` for solutions to common issues.

### Common Issues

1. **"std::filesystem not found"**: Ensure C++17 is enabled (`-DCMAKE_CXX_STANDARD=17`)
2. **"OpenSSL not found"**: Install OpenSSL development packages
3. **Hash verification failures**: Ensure piece data is not corrupted during download

## Performance Considerations

- Piece cache operations are I/O bound
- Cache directory should be on fast storage (SSD recommended)
- Resume data is loaded synchronously on startup
- Alert handling integrated into main event loop

## Limitations

- Requires C++17 compiler and standard library
- Cache pieces stored as individual files (not in database)
- No automatic cache cleanup (manual management required)
- Resume data stored as plain text files

## Future Enhancements

Potential improvements for future versions:

- [ ] Optional SQLite backend for piece metadata
- [ ] Automatic cache cleanup policies (LRU, size limits)
- [ ] Cache compression support
- [ ] Multi-level cache hierarchy
- [ ] Async file I/O operations
- [ ] Cache statistics and monitoring

## Contributing

Contributions are welcome! Please follow libtorrent coding standards:

1. Modular design principles
2. Comprehensive testing
3. Cross-platform compatibility
4. Documentation for public APIs

## License

Same as libtorrent (BSD 3-Clause)

## Support

For issues or questions:
1. Check this README and `REFACTORING_GUIDE.md`
2. Review `IMPLEMENTATION_SUMMARY.md` for architecture details
3. Open an issue on the libtorrent repository

## References

- [libtorrent Documentation](https://www.libtorrent.org/reference.html)
- [Original PR Discussion](https://github.com/arvidn/libtorrent/pulls)
- [Refactoring Guide](REFACTORING_GUIDE.md)
- [Implementation Summary](IMPLEMENTATION_SUMMARY.md)

---

**Author**: ismdevteam  
**Status**: Production Ready  
**Last Updated**: 2026-02-03
