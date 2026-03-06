# BitTorrent v2 Torrents (BEP 52)

## Info Dict Structure

V2 uses a nested **`file tree`** dict instead of a flat `files` list:
- Keys are filename path components; non-leaf = directory (nested dict)
- Leaf nodes have empty-string key `""` → file metadata dict with `length`, `pieces root`, etc.
- **`pieces root`** (32 bytes): root of the per-file merkle tree (SHA-256)
- **`meta version`**: integer `2` identifies v2 metadata
- **`piece layers`**: (outside info dict) maps `pieces root` → concatenated piece-layer hashes

V2 torrents have a **SHA-256 info hash** (vs v1's SHA-1). `torrent_info::m_info_hash` holds both.

## Per-File Merkle Tree

Each file has an independent merkle tree (SHA-256, not SHA-1):

```
root (pieces root)
  └── interior nodes
        └── piece layer  ← hash of one piece's block subtree
              └── block layer (leaf) ← hash of 16 KiB block
```

- **Block** = 16 KiB (fixed), the unit of transfer
- **Piece** = `piece_length` bytes; must be a power-of-2 multiple of 16 KiB
- Tree is **padded to a power of 2** in leaf count using `merkle_pad()` (zeros hashed up)
- Interior nodes: `parent = SHA-256(left_child || right_child)`
- Flat array indexing: node at layer `l`, offset `o` → index `(1 << l) - 1 + o`; parent = `(i-1)/2`

Key helpers in `src/merkle.cpp`:
- `merkle_num_leafs()`, `merkle_num_nodes()`, `merkle_num_layers()`
- `merkle_fill_tree()` — compute interior nodes from leaves
- `merkle_validate_and_insert_proofs()` — validate a proof chain against a known root
- `merkle_pad()` — padding hash for odd tree levels

## merkle_tree Class (`include/libtorrent/aux_/merkle_tree.hpp`)

One instance per file, with four storage modes (optimization):

| Mode | Stored nodes | When used |
|------|-------------|-----------|
| `empty_tree` | root only | single-block file, or tree completely unknown |
| `piece_layer` | piece-layer hashes | piece hashes received, blocks not yet |
| `block_layer` | block hashes | download complete, all blocks verified |
| `full_tree` | all nodes | during partial verification |

`m_block_verified` bitfield tracks which block hashes have been validated against their parent chain up to the known root.

Key methods: `load_tree()`, `load_piece_layer()`, `add_hashes()`, `set_block()`

## Hybrid Torrents (v1 + v2)

A single `.torrent` file can contain both:
- v1 info dict: `files` list + `pieces` SHA-1 concatenation
- v2 `file tree` + `piece layers`

Constraints: identical file layout, same `piece_length`. Results in two info hashes.
`torrent_info` parsing uses `extract_files()` for v1 and `extract_files2()` for v2.

## Hash Picker (`src/hash_picker.cpp`)

Decides which merkle hashes to request from peers (BitTorrent v2 hash exchange protocol).

**`hash_request` struct:**
```cpp
struct hash_request {
  file_index_t file;
  int base;         // tree layer (0 = block layer)
  int index;        // starting offset in layer
  int count;        // number of hashes (≤ 8192)
  int proof_layers; // uncle hashes needed to prove against known root
};
```

**Strategy:**
1. Request piece-layer hashes in 512-piece chunks until all known
2. On piece failure, request all block hashes for that piece (3s minimum interval)

**Validation flow:**
- `add_hashes()`: receives hashes + uncle proof chain, calls `merkle_validate_and_insert_proofs()`, inserts valid nodes, reports `pass`/`fail` per piece
- `set_block()`: inserts a single block hash, finds largest verifiable subtree, returns pass/fail

## Resume Data

Merkle trees are persisted across sessions in resume data (`src/write_resume_data.cpp`, `src/read_resume_data.cpp`):

```
["merkle trees"][file_index]      → sparse tree nodes (non-zero nodes only)
["merkle tree mask"][file_index]  → bitfield of which nodes are stored
["verified bits"][file_index]     → m_block_verified bitfield
["piece layers"][pieces_root]     → concatenated piece-layer hashes
```

- `write_resume_data()` uses `build_sparse_vector()` to encode only known nodes
- `read_resume_data()` calls `load_sparse_tree()` to restore the partial tree
- Piece layers are re-derived from the stored tree during write

## Verification Flow

1. File added → `merkle_tree` created with root hash from `pieces root`
2. Block arrives → `set_block()` inserts hash, attempts validation up to root
3. Piece complete → all block hashes in piece set; piece passes if tree validates
4. Piece fails → clear invalid nodes; hash picker schedules block-hash requests
5. Hashes received → `add_hashes()` validates proof chain, inserts valid nodes, marks failures
6. Download complete → all blocks verified; block layer mode; tree fully populated

## Key Files

| File | Purpose |
|------|---------|
| `src/merkle.cpp` | Tree math, proof validation, padding |
| `src/merkle_tree.cpp` + `include/libtorrent/aux_/merkle_tree.hpp` | Per-file tree storage |
| `src/hash_picker.cpp` + `include/libtorrent/aux_/hash_picker.hpp` | Hash request scheduling |
| `src/torrent_info.cpp` | Parsing `file tree`, `piece layers`, hybrid logic |
| `src/create_torrent.cpp` | Building v2/hybrid `.torrent` files |
| `src/write_resume_data.cpp` + `src/read_resume_data.cpp` | Persisting trees |
