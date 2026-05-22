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
| `gen_settings_doc.py` | `settings.rst` + appends settings names to `hunspell/settings.dic` | `settings_pack.hpp` |
| `gen_stats_doc.py` | `stats_counters.rst` | `session_stats.cpp`, `performance_counters.hpp` |
| `gen_todo.py` | `todo.html` | `src/*.cpp`, headers |
| `filter-rst.py` | `*-plain.txt` (prose-only) | a hand-written `.rst` file |

`gen_settings_doc.py` doubles as a dictionary source: it splits every setting
name on `_` and adds the parts to `hunspell/settings.dic`, so setting-name
fragments are not flagged as misspellings.

## Spell Checking (hunspell)

Spell checking uses **hunspell**, run via `make spell-check` from `docs/`:

```sh
cd docs && make spell-check
```

The flow:
1. Generated reference prose is in `plain_text_out.txt` (from
   `gen_reference_doc.py --plain-output`).
2. Hand-written manuals are converted to prose-only text with
   `filter-rst.py` (e.g. `manual.rst` -> `manual-plain.txt`). `filter-rst.py`
   strips RST directives (`.. ` lines) and indented literal/code blocks so
   only natural-language text remains.
3. `hunspell -l` lists misspelled words from each plain-text file into
   `hunspell-report.txt`, using dictionary `hunspell/en_US` plus the project
   word list `hunspell/libtorrent.dic` (settings text uses the generated
   `hunspell/settings.dic`). HTML manuals are checked with `-H` (HTML mode).
4. If `hunspell-report.txt` is non-empty the target **fails** and prints the
   offending words.

### Fixing spell-check failures

Either correct the typo in the source, or -- if the word is a real term,
identifier, or acronym -- add it to **`docs/hunspell/libtorrent.dic`** (one
word per line). `hunspell/en_US.{aff,dic}` is the stock English dictionary and
should not be edited. `hunspell/settings.dic` is generated (en_US + the
settings names + a copy of `libtorrent.dic`); do not hand-edit it.

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

`.github/workflows/docs.yml` installs `python3-docutils`, `hunspell`,
`graphviz`, `imagemagick`, `aafigure`, etc., then runs
`make spell-check html`. A documentation warning, an undocumented public
symbol, a `TODO:` in a doc comment, or a spelling error all fail the build.

## Key Files

| File | Purpose |
|------|---------|
| `docs/gen_reference_doc.py` | parse headers -> reference RST + plain text |
| `docs/gen_settings_doc.py` | `settings.rst` + settings dictionary words |
| `docs/gen_stats_doc.py` | `stats_counters.rst` |
| `docs/filter-rst.py` | strip RST markup to prose for spell checking |
| `docs/makefile` | `html`, `rst`, `spell-check`, `stage`, `clean` targets |
| `docs/hunspell/libtorrent.dic` | project word list (edit to whitelist terms) |
| `docs/hunspell/en_US.{aff,dic}` | stock English dictionary (do not edit) |
| `.github/workflows/docs.yml` | CI: spell-check + build |
