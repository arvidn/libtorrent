# Piece Cache Feature for libtorrent

This feature adds fileless torrent operation capability to libtorrent, allowing seeding without creating original content files using a piece-based cache system.

## Features

- **Fileless Operation (-Z mode)**: Seed torrents without creating original content files
- **Seed-from-Cache Mode (-S mode)**: Seed exclusively from piece cache without creating any original files
- **Piece-based Caching**: Store pieces in a cache directory instead of original files
- **Resume Support**: Maintain resume data in cache directory
- **Backward Compatible**: Works alongside traditional file storage

## Files Added

- `examples/piece_cache_manager.hpp` - Main cache management interface
- `examples/piece_cache_manager.cpp` - Cache implementation with disk storage
- `examples/simple_piece_cache.hpp` - Integration layer with libtorrent session
- `examples/client_test_piece_cache.cpp` - Enhanced client with piece cache support

## Usage

### Building
```bash
git clone --recurse-submodules -b piece-cache-feature https://github.com/ismdevteam/libtorrent.git
cd ./libtorrent
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -G Ninja .. -Dbuild_examples=ON
ninja client_test_piece_cache
```

### Command Line Options
- `-Z` - Enable fileless mode (disable original content storage)
- `-S` - Seed from piece cache only (no original files created at all)
- `-C` - Cache pieces during download
- `--cache_root=<path>` - Set custom cache directory

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

### -S Mode (Seed-from-Cache Only)
- **Pure seeding mode**: No original files created at any point
- Requires existing cache data from previous download
- Ideal for long-term seeding without file system footprint
- Most privacy-friendly option

## Benefits

- **Disk Space Efficiency**: Store only cached pieces, not entire files
- **Privacy**: No original content files created on disk
- **Performance**: Reduced disk I/O for seeding-only scenarios
- **Flexibility**: Can switch between fileless and traditional modes
- **Pure Seeding**: -S mode enables seeding without any file creation

## Implementation Details

The piece cache intercepts piece downloads and stores them in a structured cache directory:
```text
cache_root/
├── [info_hash]/
│   ├── metadata.txt
│   ├── piece_000001.dat
│   └── piece_000002.dat
└── .resume/
    └── [info_hash].resume
```

## Compatibility

- libtorrent 2.0.11+
- C++17 compatible compilers
- Tested on Linux, should work on Windows/macOS with minor adjustments

## Testing with libtorrent v2.0.11

This feature has been extensively tested with **libtorrent v2.0.11**:

**Test Environment:**
- libtorrent Version: v2.0.11
- Compiler: GCC 11.3.0 / Clang 14.0.0
- Platform: Linux x86_64
- Build System: CMake 3.22 + Ninja 1.10

**Test Coverage:**
- ✅ Fileless mode (-Z flag) functionality
- ✅ Seed-from-cache mode (-S flag) functionality
- ✅ Piece caching during download (-C flag)
- ✅ Resume data management in cache directory
- ✅ Both torrent file and magnet link support
- ✅ Seeding from cache without original files
- ✅ Memory and disk cache consistency
- ✅ Error handling and edge cases
