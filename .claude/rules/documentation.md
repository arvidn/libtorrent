# Documentation Generation and Spell Checking

The reference documentation is generated from the C++ public headers; the
prose manual is hand-written reStructuredText (RST). Both are built and
spell-checked from the `docs/` directory via `docs/makefile`, and the same
targets run in CI (`.github/workflows/docs.yml`).

## Reference Doc Generation (`docs/gen_reference_doc.py`)

`gen_reference_doc.py` parses the public headers and emits RST. It scans
`include/libtorrent/*.hpp`, `include/libtorrent/kademlia/*.hpp`, and
`include/libtorrent/extensions/*.hpp` (add `--internal` to also include
`include/libtorrent/aux_/*.hpp`). For each documented class, function, enum,
and constant it produces an RST file per category (`reference-Core.rst`,
`reference-Session.rst`, ...) plus a table of contents (`reference.rst`) and an
optional `single-page-ref.rst` (`--single-page`).

### How comments become docs

- A documentation comment is the run of `//` comment lines immediately
  **above** a declaration, with no blank line between the comment and the
  declaration. A blank line, or a line that ends a declaration (`;`, `{`,
  `}`), resets the pending comment.
- **Comment bodies are reStructuredText.** You can use RST markup directly in
  the `//` comments: `.. code::` blocks, `.. note::`, bullet lists,
  `parsed-literal`, headings, etc. The generator renders them verbatim into
  the output RST.
- **Symbol cross-linking** is automatic: any word in a comment that matches a
  known symbol name (class, function, enum, `settings_pack::` setting, or one
  of the pre-defined manual anchors) is turned into a hyperlink by
  `linkify_symbols()`. A trailing underscore (`foo_`) forces a link the same
  way RST reference syntax does.

### Grouping functions under one comment (man-page style)

Multiple overloads or related functions can share a **single** documentation
comment. The rule is **adjacency with no blank line between the
declarations**: when a function (or field) declaration immediately follows the
previous one with no blank line and no intervening comment, its signature is
appended to the previous entry's signature list rather than starting a new
entry. So a comment above the first of a group documents the whole group, the
way a man page documents a family of related calls together. Inserting a blank
line between declarations splits them into separately-documented entries (and
the second will then warn if it has no comment of its own).

### Visibility and warnings

- A comment beginning with `hidden` excludes the symbol entirely. A comment
  beginning with `internal` excludes it from the public docs (but `--internal`
  includes it).
- Every public, non-trivial member must be documented. Undocumented
  symbols print `WARNING: ... is not documented`. Trivial members
  (destructors, default/copy/move ctors, assignment, comparison/stream/
  conversion operators) are exempt via `is_trivial_member()`.
- A `TODO:` in a public doc comment is a hard error (exits non-zero).
- `#if` blocks gated on debug-only macros (asserts, invariant checks, and the
  other debugging macros) and on old `TORRENT_ABI_VERSION` guards are skipped
  entirely, so debug-only or deprecated members do not appear in docs. A
  non-ABI `TORRENT_*` `#if` inside a public struct warns about possible ABI
  breakage.
- `// OVERVIEW` introduces a section overview block for a header's category.

### Plain-text output for spell checking

With `--plain-output`, `add_desc()` writes a `plain_text_out.txt` containing
**only the prose** from doc comments, with the C++ signatures and `.. code::`
blocks stripped out. This is what the spell checker runs over.

## Other Generators

| Script | Output | Source |
|--------|--------|--------|
| `gen_reference_doc.py` | `reference-*.rst`, `reference.rst`, `single-page-ref.rst`, `plain_text_out.txt` | public headers |
| `gen_settings_doc.py` | `settings.rst` + appends settings names to `settings.dic` | `settings_pack.hpp` |
| `gen_stats_doc.py` | `stats_counters.rst` | `session_stats.cpp`, `performance_counters.hpp` |
| `gen_todo.py` | `todo.html` | `src/*.cpp`, headers |
| `filter-rst.py` | `*-plain.txt` (prose-only) | a hand-written `.rst` file |

`gen_settings_doc.py` doubles as a dictionary source: it splits every setting
name on `_` and adds the parts to `settings.dic`, so setting-name
fragments are not flagged as misspellings.

## Spell Checking (cspell)

Spell checking uses **[cspell](https://cspell.org/)**, run via `make spell-check`
from `docs/`. cspell is pinned to an exact version via `docs/package.json` +
`docs/package-lock.json` (the lockfile records a sha512 integrity hash for
every package in cspell's dependency tree, not just cspell itself), so
install it once with `npm ci` before running the checker for the first time
or after the pin changes:

```sh
cd docs && npm ci
cd docs && make spell-check
```

`docs/makefile` invokes the locally-installed binary at
`node_modules/.bin/cspell` (overridable via `make CSPELL=cspell spell-check`
to use a system-wide install instead).

The flow:
1. Generated reference prose is in `plain_text_out.txt` (from
   `gen_reference_doc.py --plain-output`).
2. Hand-written manuals are converted to prose-only text with
   `filter-rst.py` (e.g. `manual.rst` -> `manual-plain.txt`). `filter-rst.py`
   strips RST directives (`.. ` lines) and indented literal/code blocks so
   only natural-language text remains.
3. `cspell` checks every plain-text file plus the built HTML manuals in one
   invocation, using `docs/cspell.json` (which loads the project word list
   `settings.dic`, a superset of `libtorrent.dic` with
   setting-name fragments added). cspell reports each misspelling as
   `file:line:col`, unlike the old hunspell-based check, which only produced a
   flat, location-less word list.
4. `cspell` exits non-zero on any misspelling, which fails the `make` target
   directly.

### Fixing spell-check failures

Either correct the typo in the source, or -- if the word is a real term,
identifier, or acronym -- add it to **`docs/libtorrent.dic`** (one
word per line). `docs/settings.dic` is generated (setting-name
fragments + a copy of `libtorrent.dic`); do not hand-edit it. cspell's stock
English dictionaries are bundled with the tool and are not vendored in the
repo.

## Building the HTML

```sh
cd docs && make html        # all RST -> HTML via rst2html, plus figures
cd docs && make             # default target builds the docs
```

HTML is produced by `rst2html` (docutils) with `template.txt`/`style.css`;
figures are rendered from `.dot` (graphviz), `.diagram` (aafigure), and `.svg`
(imagemagick `convert`). `make stage` copies the built site to `$WEB_PATH`.
`make clean` removes generated `.rst`/`.html`/figures (it does **not** delete
the checked-in hand-written `.rst` manuals).

## CI

`.github/workflows/docs.yml` installs `python3-docutils`, `graphviz`,
`imagemagick`, `aafigure`, etc., runs `npm ci` in `docs/` to install the
pinned `cspell`, then runs `make spell-check html`. A documentation warning,
an undocumented public symbol, a `TODO:` in a doc comment, or a spelling
error all fail the build.

## Key Files

| File | Purpose |
|------|---------|
| `docs/gen_reference_doc.py` | parse headers -> reference RST + plain text |
| `docs/gen_settings_doc.py` | `settings.rst` + settings dictionary words |
| `docs/gen_stats_doc.py` | `stats_counters.rst` |
| `docs/filter-rst.py` | strip RST markup to prose for spell checking |
| `docs/makefile` | `html`, `rst`, `spell-check`, `stage`, `clean` targets |
| `docs/cspell.json` | cspell config (loads `settings.dic`) |
| `docs/package.json` / `docs/package-lock.json` | pins cspell + its dependency tree by version and integrity hash |
| `docs/libtorrent.dic` | project word list (edit to whitelist terms) |
| `docs/settings.dic` | generated: `libtorrent.dic` + setting-name fragments |
| `.github/workflows/docs.yml` | CI: spell-check + build |
