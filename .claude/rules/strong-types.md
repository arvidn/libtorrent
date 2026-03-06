---
paths:
  - "include/libtorrent/units.hpp"
  - "include/libtorrent/flags.hpp"
  - "include/libtorrent/index_range.hpp"
  - "include/libtorrent/**"
  - "src/**"
---

## Strong Types

The codebase avoids using raw `int` or bare integers for indices and flags. Instead, use the strong type and flag facilities.

**Index types** (`include/libtorrent/units.hpp`):

`aux::strong_typedef<UnderlyingType, Tag>` wraps an integer so it is incompatible with other integers and other strong types. Arithmetic with raw integers is not allowed; only arithmetic with the same type or its `diff_type` is permitted.

To define a new index type:
```cpp
using my_index_t = aux::strong_typedef<std::int32_t, struct my_index_tag>;
```

To convert to/from the underlying integer, use an explicit cast:
```cpp
piece_index_t p{42};
int raw = static_cast<int>(p);
```

To iterate over a range of indices, use `index_range` (`include/libtorrent/index_range.hpp`):
```cpp
for (piece_index_t i : index_range<piece_index_t>{begin, end}) { ... }
```

**Flag types** (`include/libtorrent/flags.hpp`):

`flags::bitfield_flag<UnderlyingType, Tag>` wraps an unsigned integer as a type-safe bitfield. Flags of different types cannot be combined. Individual bit constants are defined using the `_bit` user-defined literal:

```cpp
using torrent_flags_t = flags::bitfield_flag<std::uint64_t, struct torrent_flags_tag>;
static constexpr torrent_flags_t seed_mode = 0_bit;
static constexpr torrent_flags_t upload_mode = 1_bit;
```

To define a new flag type:
```cpp
using my_flags_t = flags::bitfield_flag<std::uint32_t, struct my_flags_tag>;
```

Flag values support `|`, `&`, `^`, `~`, `|=`, `&=`, `^=`, and `operator bool()` (true if any bit is set). To test a flag:
```cpp
if (flags & torrent_flags::seed_mode) { ... }
```
