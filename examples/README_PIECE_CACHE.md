# Piece Cache Feature for libtorrent

This feature adds fileless torrent operation capability to libtorrent, allowing seeding without creating original content files.

## Features

- **Fileless Operation (-Z mode)**: Seed torrents without creating original content files
- **Piece-based Caching**: Store pieces in a cache directory instead of original files
- **Resume Support**: Maintain resume data in cache directory
- **Backward Compatible**: Works alongside traditional file storage

## Files Added

- `piece_cache_manager.hpp` - Main cache management interface
- `piece_cache_manager.cpp` - Cache implementation with disk storage
- `simple_piece_cache.hpp` - Integration layer with libtorrent session
- `client_test_piece_cache.cpp` - Enhanced client with piece cache support

## Usage

### Building
```bash
git clone --recurse-submodules -b piece-cache-feature https://github.com/ismdevteam/libtorrent.git
cd ./libtorrent
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -G Ninja .. -Dbuild_examples=ON
ninja client_test_piece_cache

Command Line Options

    -Z - Enable fileless mode (disable original content storage)

    -C - Cache pieces during download

    --cache_root=<path> - Set custom cache directory
```
## Examples

```bash
# Fileless mode with a torrent file
./client_test_piece_cache -Z -s /tmp/save_path torrent_file.torrent

# Fileless mode with magnet link
./client_test_piece_cache -Z -s /tmp/save_path "magnet:?xt=urn:btih:..."
```

Benefits

    Disk Space Efficiency: Store only cached pieces, not entire files

    Privacy: No original content files created on disk

    Performance: Reduced disk I/O for seeding-only scenarios

    Flexibility: Can switch between fileless and traditional modes

Implementation Details

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

Compatibility

    libtorrent 2.0.12+

    C++17 compatible compilers

    Tested on Linux, should work on Windows/macOS with minor adjustments
