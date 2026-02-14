# dump_torrent.py - Python Implementation of dump_torrent

A pure Python implementation of the `dump_torrent` utility using libtorrent's Python bindings. This script provides the exact same functionality as the original C++ `dump_torrent` binary but as a Python script that can be easily modified and extended.

## Features

- **Complete dump_torrent Compatibility**: Produces identical output to the original C++ binary
- **JSON Output**: Optional pretty-printed JSON output with additional metadata
- **Flexible libtorrent Loading**: Can use system libtorrent or a custom build
- **Comprehensive Metadata**: Includes file statistics, extensions, and detailed file info
- **v1 and v2 Torrent Support**: Full support for both torrent versions
- **Pad File Handling**: Optional inclusion of pad files in output
- **Error Resilience**: Graceful handling of corrupt or invalid torrents

## Files

- `examples/dump_torrent.py` - Main Python script
- `examples/README_DUMP_TORRENT_PY.md` - This documentation

## Prerequisites

- Python 3.6 or higher
- libtorrent Python bindings (either system-installed or custom build)

## Installation Options

### Option 1: Using System libtorrent

```bash
# On Debian/Ubuntu
sudo apt-get install python3-libtorrent

# On Fedora
sudo dnf install python3-libtorrent

# On Arch Linux
sudo pacman -S python-libtorrent
```

### Option 2: Building libtorrent with Python Bindings

```bash
git clone --recurse-submodules https://github.com/arvidn/libtorrent.git
cd libtorrent
mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=17 \
      -Dpython-bindings=ON \
      -G Ninja ..

ninja -j$(nproc)
```

The Python bindings will be in `build/bindings/python/`

## Usage

```bash
# Basic usage - text output (like original dump_torrent)
./examples/dump_torrent.py /path/to/torrent.torrent --dump

# Pretty-printed JSON output
./examples/dump_torrent.py /path/to/torrent.torrent --pretty

# Save output to file
./examples/dump_torrent.py /path/to/torrent.torrent --pretty -o metadata.json

# Include pad files in output
./examples/dump_torrent.py /path/to/torrent.torrent --dump --show-padfiles

# Use custom libtorrent path
./examples/dump_torrent.py /path/to/torrent.torrent \
  --libtorrent-path /path/to/libtorrent/build/bindings/python \
  --dump

# Set limits for large torrents
./examples/dump_torrent.py /path/to/torrent.torrent \
  --items-limit 10000 \
  --depth-limit 100 \
  --max-pieces 10000 \
  --pretty
```

## Command Line Options

| Option | Description |
|--------|-------------|
| `torrent_file` | Torrent file to analyze (required) |
| `-p, --pretty` | Pretty-print JSON output |
| `-o, --output FILE` | Output file (default: stdout) |
| `-d, --dump` | Output in dump_torrent format (text) |
| `--show-padfiles` | Include pad files in output |
| `--libtorrent-path PATH` | Custom path to libtorrent Python bindings |
| `--items-limit COUNT` | Upper limit of bencode items |
| `--depth-limit COUNT` | Recursion limit in bdecoder |
| `--max-pieces COUNT` | Upper limit on number of pieces |
| `--max-size SIZE_MB` | Upper limit on file size in MiB |

## Output Formats

### Text Mode (--dump)
Produces output identical to the original C++ `dump_torrent` binary:
```
number of pieces: 13
piece length: 1048576
info hash: 33e3aedf097bbc413c32deb5d9fd1c82defce9f8, 4a6e761d52eba939809d768478cd86cedefc649096ec6c623e356aa2cb18e34b
comment: 
created by: qBittorrent v5.1.0
magnet link: magnet:?xt=urn:btih:33e3aedf097bbc413c32deb5d9fd1c82defce9f8&xt=urn:btmh:12204a6e761d52eba939809d768478cd86cedefc649096ec6c623e356aa2cb18e34b
name: 
number of files: 3
files:
        0          30 ---- [     0,     0 ]       0 c37f49c6992bc42229a8f346d71925c33f47cd00dcab76ddea65cfcf946d8c80 torrent1/README.txt
   100000    12582912 ---- [     1,    12 ]       0 ca419f5f355d797a92dc108db17cc10543f18cdf66d5b3e8754d70205d9db7a7 torrent1/blob1.bin
web seeds:
```

### JSON Mode (--pretty)
Provides additional metadata and statistics:
```json
{
  "torrent file": "/path/to/torrent.torrent",
  "file_size": 1153,
  "number of pieces": 13,
  "piece length": 1048576,
  "info hash": "33e3aedf097bbc413c32deb5d9fd1c82defce9f8, 4a6e761d52eba939809d768478cd86cedefc649096ec6c623e356aa2cb18e34b",
  "name": "",
  "total size": 12582942,
  "total_files": 2,
  "non-pad files": 2,
  "pad files": 1,
  "executable files": 0,
  "symlink files": 0,
  "largest_file": 12582912,
  "smallest_file": 30,
  "average_file_size": 6291471.0,
  "file_extensions": {
    ".txt": 1,
    ".bin": 1
  },
  "files": [
    {
      "index": 0,
      "offset": 0,
      "offset_hex": "0",
      "size": 30,
      "flags": "----",
      "is_pad": false,
      "piece_range": [0, 0],
      "path": "torrent1/README.txt",
      "root_hash": "c37f49c6992bc42229a8f346d71925c33f47cd00dcab76ddea65cfcf946d8c80"
    }
  ]
}
```

## Integration with Other Tools

### Using in Scripts
```python
import subprocess
import json

# Get JSON output
result = subprocess.run(
    ['./examples/dump_torrent.py', 'torrent.torrent', '--pretty'],
    capture_output=True,
    text=True
)
metadata = json.loads(result.stdout)
print(f"Torrent has {metadata['total_files']} files")
```

### Piping Output
```bash
# Get torrent info and process with jq
./dump_torrent.py torrent.torrent --pretty | jq '.files[].path'

# Extract magnet link
./dump_torrent.py torrent.torrent --pretty | jq -r '.["magnet link"]'
```

## Comparison with Original dump_torrent

| Feature | Original dump_torrent | dump_torrent.py |
|---------|----------------------|-----------------|
| Output format | Text only | Text + JSON |
| Pad files | Hidden by default | Optional `--show-padfiles` |
| File statistics | No | Yes (sizes, counts, extensions) |
| Custom libtorrent | No (uses system) | Yes (`--libtorrent-path`) |
| Python API | No | Yes (can be imported) |
| Additional metadata | No | File timestamps, statistics |

## Testing

The script has been extensively tested against the original `dump_torrent` binary using over 60 different torrent files, including:

- v1 and v2 torrents
- Torrents with pad files
- Torrents with symlinks
- Corrupt and invalid torrents
- Large torrents (5000+ pieces)
- Torrents with web seeds and trackers
- Edge cases (long paths, special characters)

## Troubleshooting

### libtorrent Not Found
```bash
# Check if libtorrent is installed
python3 -c "import libtorrent; print(libtorrent.__version__)"

# If not found, install system package or use --libtorrent-path
./dump_torrent.py torrent.torrent --libtorrent-path /custom/path
```

### Permission Denied
```bash
chmod +x examples/dump_torrent.py
```

### Python Version Issues
The script requires Python 3.6+. Check your version:
```bash
python3 --version
```
