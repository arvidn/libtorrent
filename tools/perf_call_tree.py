#!/usr/bin/env python3
# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4

# Copyright (c) 2026, Arvid Norberg
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the distribution.
#     * Neither the name of the author nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# this script processes a perf.data file (recorded by 'perf record') and
# generates a single self-contained HTML page displaying the aggregated call
# tree. the tree is navigable with the keyboard: up/down move the selection,
# left collapses a subtree (or steps to the parent), right expands it (or steps
# to the first child). every level is collapsed by default.

from argparse import ArgumentParser
from functools import lru_cache
import html
import math
from pathlib import Path
import re
import subprocess
from typing import Optional


# a node in the calling-context tree. "count" is the number of samples whose
# stack passes through this node along this exact path (inclusive count).
class Node:
    __slots__ = ("name", "count", "system", "children")

    def __init__(self, name: str) -> None:
        self.name = name
        self.count = 0
        # true if this symbol was sampled in kernel (or kernel-module) space or
        # inside the C library (libc) -- such frames are de-emphasized in the
        # tree and the auto-expand stops when it reaches them
        self.system = False
        self.children: dict[str, "Node"] = {}

    def child(self, name: str) -> "Node":
        node = self.children.get(name)
        if node is None:
            node = Node(name)
            self.children[name] = node
        return node


def run_perf_script(perf_data: Path) -> str:
    """Run 'perf script' on perf_data and return its stdout. Exits with a clear
    message if perf is missing or fails."""
    cmd = ["perf", "script", "-i", str(perf_data)]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except FileNotFoundError:
        raise SystemExit(
            "ERROR: 'perf' is not installed or not in PATH.\n"
            "  Debian/Ubuntu: sudo apt install linux-perf (or linux-tools-*)\n"
            "  Fedora/RHEL:   sudo dnf install perf"
        )
    except subprocess.CalledProcessError as e:
        raise SystemExit(
            f"ERROR: '{' '.join(cmd)}' exited with code {e.returncode}\n\n"
            f"perf output:\n{e.stderr.strip()}"
        )
    return result.stdout


# a stack frame line from 'perf script' looks like:
#             ffffffff8100a1b2 some_function+0x12 (/path/to/dso)
# capture the leading hex address and the rest (symbol + "+0xoffset" + "(dso)").
_FRAME_RE = re.compile(r"^\s+([0-9a-fA-F]+)\s+(.*)$")

# the per-sample header line (non-indented) that precedes each stack looks like:
#   client_test 12345/12348 [001] 9043.123456: 1000000 cycles:ppp:
#   ^comm        ^pid ^tid    ^cpu  ^timestamp
# comm may itself contain spaces, so anchor on the "pid[/tid] [cpu] time:" tail
# and let comm absorb everything before it. the cpu field is optional. the
# timestamp (seconds, with a fractional part) is captured to bucket samples
# into time intervals for the per-thread histograms.
_HEADER_RE = re.compile(
    r"^(?P<comm>.+?)\s+(?P<pid>\d+)(?:/(?P<tid>\d+))?\s+"
    r"(?:\[\d+\]\s+)?(?P<time>[\d.]+):"
)

# trailing cv/ref/exception qualifiers that 'perf' (via c++filt) appends after
# the parameter list of a member function, e.g. "foo(int) const noexcept".
_TRAILING_QUALS = (" const", " volatile", " noexcept", " &&", " &")


# the same symbols recur across millions of stack frames, so cache the result
# per distinct symbol -- the scans below then run only once per unique name.
@lru_cache(maxsize=None)
def strip_return_type(sym: str) -> str:
    """Strip the leading return type from a demangled C++ signature.

    The Itanium ABI only encodes a return type for template instantiations, so
    most frames (plain functions) have nothing to strip and are returned
    unchanged. For the template case the return type is the leading token,
    separated from the qualified function name by a depth-0 space:

        std::pair<a, b> ns::foo<int>(int)  ->  ns::foo<int>(int)

    The split point is the last space that sits outside any '<>' or '()'
    nesting and before the parameter list. '(anonymous namespace)' segments and
    'operator' names are handled so their internal spaces/parens do not fool the
    scan. Conversion operators ('operator bool()') and other exotic forms are
    left untouched rather than mangled."""
    # find the parameter-list '(' by matching the final ')' from the right.
    # peel off trailing qualifiers first so the final ')' is the arg list.
    body = sym
    changed = True
    while changed:
        changed = False
        for q in _TRAILING_QUALS:
            if body.endswith(q):
                body = body[: -len(q)]
                changed = True
    close = body.rfind(")")
    if close == -1:
        # not a function signature (data symbol, mangled name, C function)
        return sym
    depth = 0
    open_paren = -1
    for k in range(close, -1, -1):
        c = body[k]
        if c == ")":
            depth += 1
        elif c == "(":
            depth -= 1
            if depth == 0:
                open_paren = k
                break
    if open_paren == -1:
        return sym
    prefix = body[:open_paren]
    # the return type ends at the last depth-0 space in the prefix (depth here
    # counts both '<>' and '()' so anonymous-namespace parens are skipped).
    angle = paren = 0
    boundary = -1
    for j, c in enumerate(prefix):
        if c == "<":
            angle += 1
        elif c == ">":
            angle -= 1
        elif c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == " " and angle == 0 and paren == 0:
            boundary = j
    if boundary == -1:
        return sym
    # a trailing 'operator' before the space means this is a conversion operator
    # ('operator bool'), not a return type plus a name; leave it alone.
    if prefix[:boundary].endswith("operator"):
        return sym
    return sym[boundary + 1 :]


# the set of characters that make up a symbolic operator name ('operator<<',
# 'operator<=>', 'operator->'). used to skip past such names so their angle
# brackets are not mistaken for template-argument brackets.
_OP_CHARS = frozenset("<>=!+-*/%^&|~")


@lru_cache(maxsize=None)
def collapse_templates(sym: str) -> str:
    """Replace every top-level template argument list with '<...>'.

    Matching pairs of '<' and '>' are folded so that, e.g.

        std::map<int, std::vector<foo>>::find(char const*) const
        ->  std::map<...>::find(char const*) const

    Only outermost (depth-0) '<>' pairs are emitted as '<...>'; anything nested
    inside them is dropped. Symbolic operators whose name contains '<' or '>'
    ('operator<<', 'operator<=>', 'operator->') are copied verbatim so they do
    not start a spurious bracket group."""
    out: list[str] = []
    i = 0
    n = len(sym)
    depth = 0
    while i < n:
        # at depth 0, detect an 'operator' token whose symbolic name may
        # contain '<' or '>' characters that must not be treated as brackets
        if (
            depth == 0
            and sym.startswith("operator", i)
            and (i == 0 or not (sym[i - 1].isalnum() or sym[i - 1] == "_"))
        ):
            out.append("operator")
            j = i + len("operator")
            # symbolic operators follow immediately; copy the run of operator
            # characters verbatim (e.g. '<<', '<=>', '->')
            while j < n and sym[j] in _OP_CHARS:
                out.append(sym[j])
                j += 1
            i = j
            continue
        c = sym[i]
        if c == "<":
            if depth == 0:
                out.append("<...>")
            depth += 1
        elif c == ">":
            if depth > 0:
                depth -= 1
        elif depth == 0:
            out.append(c)
        i += 1
    return "".join(out)


def frame_dso(payload: str) -> Optional[str]:
    """Return the originating DSO from a frame's payload, or None. A frame line
    ends with the DSO in parentheses, e.g. 'foo+0x1 (/lib/libc.so.6)'."""
    payload = payload.rstrip()
    if not payload.endswith(")"):
        return None
    open_paren = payload.rfind(" (")
    if open_paren == -1:
        return None
    return payload[open_paren + 2 : -1]


def is_kernel_frame(addr: str, payload: str) -> bool:
    """Classify a frame as kernel (or kernel-module) space.

    Kernel code lives in the upper half of the virtual address space: on
    x86-64/arm64 every kernel address has bit 63 set, while user-space
    addresses never do, so the sampled instruction pointer alone is a reliable
    signal (it also covers kernel modules, which load at high addresses too).
    'perf' additionally tags resolved kernel frames with the
    '[kernel.kallsyms]' dso, which we accept as a backup."""
    try:
        if int(addr, 16) & (1 << 63):
            return True
    except ValueError:
        pass
    return frame_dso(payload) == "[kernel.kallsyms]"


# basename of the C runtime shared object: 'libc.so.6', 'libc-2.31.so',
# 'libc.musl-x86_64.so.1', etc.
_LIBC_RE = re.compile(r"^libc[.-]")


def is_libc_frame(payload: str) -> bool:
    """True if the frame originates from the C library (libc), identified by
    its DSO basename."""
    dso = frame_dso(payload)
    if dso is None:
        return False
    return _LIBC_RE.match(dso.rsplit("/", 1)[-1]) is not None


def frame_symbol(
    addr: str,
    payload: str,
    strip_ret: bool = True,
    collapse_tmpl: bool = True,
    abbrev_ns: bool = True,
) -> str:
    """Build a merge key (function name only) from a frame line. Strips the
    trailing '+0xoffset' and '(dso)'. Frames whose symbol is unresolved
    ('[unknown]') are kept distinct by address so unrelated functions do not
    collapse into a single tree node. When strip_ret is set, the leading return
    type of demangled C++ template signatures is also removed. When
    collapse_tmpl is set, top-level template argument lists are folded to
    '<...>'. When abbrev_ns is set, the 'libtorrent::' namespace prefix is
    abbreviated to 'lt::'."""
    payload = payload.strip()
    dso: Optional[str] = None
    # split off the trailing "(/path/to/dso)" if present
    if payload.endswith(")"):
        open_paren = payload.rfind(" (")
        if open_paren != -1:
            dso = payload[open_paren + 2 : -1]
            payload = payload[:open_paren].rstrip()
    # drop a trailing "+0x..." offset
    plus = payload.rfind("+0x")
    if plus != -1:
        payload = payload[:plus].rstrip()
    if payload == "" or payload == "[unknown]":
        base = dso.rsplit("/", 1)[-1] if dso else "?"
        return f"[unknown] 0x{addr} ({base})"
    if strip_ret:
        payload = strip_return_type(payload)
    if collapse_tmpl:
        payload = collapse_templates(payload)
    if abbrev_ns:
        payload = payload.replace("libtorrent::", "lt::")
    return payload


def parse_samples(
    text: str,
    strip_ret: bool = True,
    collapse_tmpl: bool = True,
    abbrev_ns: bool = True,
) -> tuple[list[list[tuple[str, bool]]], list[tuple[str, float]]]:
    """Parse 'perf script' output into a list of call stacks. Each stack is a
    list of (function name, is_system) pairs ordered root -> leaf, where
    is_system marks kernel or libc frames. A synthetic frame identifying the
    originating thread (comm + tid) is prepended to every stack, so each thread
    gets its own root in the resulting tree.

    Also returns a parallel list of (thread, timestamp) events -- one per
    sample that carried a timestamp -- used to build the per-thread histograms.
    """
    samples: list[list[tuple[str, bool]]] = []
    events: list[tuple[str, float]] = []
    frames: list[tuple[str, bool]] = []
    have_record = False
    thread: Optional[str] = None
    ts: Optional[float] = None

    def flush() -> None:
        nonlocal frames, have_record
        if have_record and frames:
            # perf prints leaf first; reverse to root -> leaf
            name = thread if thread is not None else "[unknown thread]"
            frames.reverse()
            # the synthetic thread root is never a system frame
            frames.insert(0, (name, False))
            samples.append(frames)
            if ts is not None:
                events.append((name, ts))
        frames = []
        have_record = False

    for line in text.splitlines():
        if line.strip() == "":
            flush()
            continue
        m = _FRAME_RE.match(line)
        if m is None:
            # a non-indented line is the per-sample header (comm/pid/tid/event).
            # it begins a new record; flush the previous one first, then capture
            # the thread identity and timestamp this record's frames belong to.
            flush()
            have_record = True
            hm = _HEADER_RE.match(line)
            if hm is not None:
                tid = hm.group("tid") or hm.group("pid")
                thread = f"{hm.group('comm').strip()} (tid {tid})"
                ts = float(hm.group("time"))
            else:
                thread = "[unknown thread]"
                ts = None
            continue
        addr, payload = m.group(1), m.group(2)
        frames.append(
            (
                frame_symbol(addr, payload, strip_ret, collapse_tmpl, abbrev_ns),
                is_kernel_frame(addr, payload) or is_libc_frame(payload),
            )
        )
    flush()
    return samples, events


def build_tree(samples: list[list[tuple[str, bool]]]) -> Node:
    """Fold call stacks into a calling-context tree with inclusive counts."""
    root = Node("[root]")
    for stack in samples:
        root.count += 1
        node = root
        for name, is_system in stack:
            node = node.child(name)
            node.count += 1
            if is_system:
                node.system = True
    return root


def build_tree_weighted(stacks: list[tuple[list[str], int]]) -> Node:
    """Like build_tree() but each stack carries its own weight (e.g. an
    allocation byte count). Frames are plain strings without the system /
    libc marker that perf samples have; heaptrack and other collapsed-
    stack producers don't expose that distinction.
    """
    root = Node("[root]")
    for frames, weight in stacks:
        root.count += weight
        node = root
        for name in frames:
            node = node.child(name)
            node.count += weight
    return root


def parse_collapsed(text: str) -> list[tuple[list[str], int]]:
    """Parse 'collapsed stacks' text (the format used by FlameGraph.pl
    and emitted by `heaptrack_print -F`): one stack per line, frames
    separated by ';' from root to leaf, followed by a whitespace-
    separated integer weight.
    """
    stacks: list[tuple[list[str], int]] = []
    for line in text.splitlines():
        if not line.strip():
            continue
        # rsplit with no separator splits on any run of whitespace, so a
        # tab or multiple spaces between the stack and its weight work
        # the same as a single space. maxsplit=1 keeps any whitespace
        # inside frame names intact. A line with no whitespace fails to
        # unpack and a non-numeric weight fails int() -- both bubble up
        # rather than silently dropping the line.
        stack_part, weight_str = line.rsplit(maxsplit=1)
        weight = int(weight_str)
        frames = stack_part.split(";")
        stacks.append((frames, weight))
    return stacks


def main_collapsed(
    collapsed: Path,
    output: Path,
    threshold: float = 0.1,
    title: Optional[str] = None,
) -> None:
    """Render an HTML call tree from a collapsed-stacks text file (the
    format FlameGraph.pl and `heaptrack_print -F` emit). Uses heap mode:
    weights are reported as bytes and percentages are relative to the
    global total.
    """
    stacks = parse_collapsed(collapsed.read_text(encoding="utf-8", errors="replace"))
    if not stacks:
        raise SystemExit(
            f"ERROR: no stacks found in {collapsed}."
            " Expected FlameGraph.pl 'collapsed stacks' format"
            " ('frame1;frame2;...;leaf <weight>' per line)."
        )
    root = build_tree_weighted(stacks)
    total = root.count
    prune(root, total * threshold / 100.0)
    page = gen_html(root, total, title or f"call tree: {collapsed.name}", mode="heap")
    output.write_text(page, encoding="utf-8")
    print(f"wrote {output} ({total} bytes across {len(stacks)} stacks)")


def prune(node: Node, min_count: float) -> None:
    """Drop children below min_count samples, depth first."""
    for name in list(node.children.keys()):
        child = node.children[name]
        if child.count < min_count:
            del node.children[name]
        else:
            prune(child, min_count)


def sorted_children(node: Node) -> list[Node]:
    return sorted(node.children.values(), key=lambda n: n.count, reverse=True)


def render_node(node: Node, total: int, parts: list[str]) -> None:
    """Append the <li> for node (and its subtree) to parts."""
    children = sorted_children(node)
    has_children = len(children) > 0
    pct = (100.0 * node.count / total) if total else 0.0
    cls = "has-children" if has_children else "leaf"
    if node.system:
        cls += " system"
    label = (
        f'<span class="pct">{pct:6.2f}%</span>'
        f'<span class="count">{node.count}</span>'
        f'<span class="name">{html.escape(node.name)}</span>'
    )
    parts.append(f'<li class="{cls}">')
    parts.append(f'<div class="node" tabindex="-1">{label}</div>')
    if has_children:
        # children hidden by default -> every level starts collapsed
        parts.append('<ul class="children" hidden>')
        for child in children:
            render_node(child, total, parts)
        parts.append("</ul>")
    parts.append("</li>")


PAGE_CSS = """
body { font-family: monospace; font-size: 13px; margin: 1em; color: #222; }
h1 { font-size: 1.1em; }
ul { list-style: none; margin: 0; padding-left: 1.4em; }
ul.tree { padding-left: 0; }
li { margin: 0; }
/* inline-block so the box (and the selection highlight) grows to the full
   width of the text rather than being clipped at the viewport edge; min-width
   keeps short rows highlighted out to at least the window border */
.node { display: inline-block; min-width: 100%; white-space: pre;
        cursor: default; padding: 1px 2px; border-radius: 3px;
        box-sizing: border-box; }
li.has-children > .node { cursor: pointer; }
/* caret: filled triangle that rotates when expanded */
li.has-children > .node::before { content: "\\25B6 "; color: #888; }
li.has-children.expanded > .node::before { content: "\\25BC "; color: #888; }
li.leaf > .node::before { content: "\\2003 "; }
.node.selected { background: #2b6cb0; color: #fff; outline: none; }
.node.selected .pct, .node.selected .count { color: #cfe3ff; }
.pct { color: #b04000; display: inline-block; width: 7ch; text-align: right; }
.count { color: #888; display: inline-block; width: 9ch; text-align: right;
         padding-right: 1ch; }
.name { color: inherit; }
/* dim system frames (kernel or libc) -- they are de-emphasized in the tree */
li.system > .node .name { color: #999; }
li.system > .node.selected .name { color: #fff; }
.hist { margin: 0 0 1.5em 0; }
.hist h2 { font-size: 1em; margin: 0 0 0.6em 0; }
.hist .chart-title { font-size: 12px; margin: 0.7em 0 0.1em 0; font-weight: bold; }
.hist svg { display: block; }
.hist .bar { fill: #2b6cb0; }
.hist .axis { stroke: #888; stroke-width: 1; }
.hist .grid { stroke: #e2e2e2; stroke-width: 1; }
.hist .tick-label { fill: #444; font-size: 10px; }
"""

PAGE_JS = """
(function () {
  var nodes = Array.prototype.slice.call(document.querySelectorAll('.node'));
  var selected = null;

  function liOf(node) { return node.parentNode; }
  function hasChildren(node) { return liOf(node).classList.contains('has-children'); }
  function isExpanded(node) { return liOf(node).classList.contains('expanded'); }
  function isSystem(node) { return liOf(node).classList.contains('system'); }

  function childList(node) { return liOf(node).querySelector(':scope > ul.children'); }

  function expand(node) {
    if (!hasChildren(node)) return;
    liOf(node).classList.add('expanded');
    var ul = childList(node);
    ul.hidden = false;
    // collapse degenerate chains: if there is exactly one child, expand it
    // too, recursively, until we reach a node with 0 or more than 1 children.
    // stop the auto-expand once we descend into a system frame (kernel or
    // libc) -- those are rarely interesting, so reveal the boundary but no
    // further (an explicit right-arrow/click on it still expands it).
    var childLis = ul.querySelectorAll(':scope > li');
    if (childLis.length === 1) {
      var only = childLis[0].querySelector(':scope > .node');
      if (!isSystem(only)) expand(only);
    }
  }
  function collapse(node) {
    if (!hasChildren(node)) return;
    liOf(node).classList.remove('expanded');
    childList(node).hidden = true;
  }

  function firstChild(node) {
    var ul = childList(node);
    if (!ul) return null;
    var li = ul.querySelector(':scope > li');
    return li ? li.querySelector(':scope > .node') : null;
  }
  function parentNodeOf(node) {
    var li = liOf(node);
    var ul = li.parentNode;
    if (!ul || !ul.classList.contains('children')) return null;
    return ul.parentNode.querySelector(':scope > .node');
  }

  // visible nodes in document order (skipping collapsed subtrees)
  function visibleNodes() {
    return nodes.filter(function (n) {
      var el = liOf(n);
      while (el && el.parentNode) {
        var ul = el.parentNode;
        if (ul.classList && ul.classList.contains('children') && ul.hidden)
          return false;
        el = ul.closest ? ul.closest('li') : null;
      }
      return true;
    });
  }

  function select(node) {
    if (!node) return;
    if (selected) selected.classList.remove('selected');
    selected = node;
    selected.classList.add('selected');
    // only scroll vertically: the rows are wider than the viewport (min-width
    // 100% plus per-level indentation), so letting scrollIntoView touch the
    // inline axis would scroll the page to the right on every selection
    var x = window.scrollX;
    selected.scrollIntoView({ block: 'nearest' });
    if (window.scrollX !== x) window.scrollTo(x, window.scrollY);
  }

  function move(delta) {
    var vis = visibleNodes();
    var idx = vis.indexOf(selected);
    if (idx === -1) { select(vis[0]); return; }
    var next = idx + delta;
    if (next >= 0 && next < vis.length) select(vis[next]);
  }

  document.addEventListener('keydown', function (e) {
    if (!selected) return;
    switch (e.key) {
      case 'ArrowDown': move(1); e.preventDefault(); break;
      case 'ArrowUp': move(-1); e.preventDefault(); break;
      case 'ArrowRight':
        if (hasChildren(selected)) {
          if (!isExpanded(selected)) expand(selected);
          else select(firstChild(selected));
        }
        e.preventDefault();
        break;
      case 'ArrowLeft':
        if (hasChildren(selected) && isExpanded(selected)) collapse(selected);
        else select(parentNodeOf(selected));
        e.preventDefault();
        break;
    }
  });

  // click toggles expand/collapse and selects
  nodes.forEach(function (node) {
    node.addEventListener('click', function () {
      select(node);
      if (hasChildren(node)) {
        if (isExpanded(node)) collapse(node); else expand(node);
      }
    });
  });

  // start with the first (hottest) top-level node selected
  if (nodes.length) select(nodes[0]);
})();
"""


def _nice_step(span: float, target: int) -> float:
    """A 'nice' (1/2/2.5/5 * 10^n) step that splits span into ~target intervals.
    Used to place axis ticks at round values."""
    if span <= 0:
        return 1.0
    raw = span / target
    mag = 10.0 ** math.floor(math.log10(raw))
    for m in (1, 2, 2.5, 5, 10):
        if m * mag >= raw:
            return m * mag
    return 10 * mag


def gen_histograms(events: list[tuple[str, float]], interval: float) -> str:
    """Render one bar chart per thread of the number of samples falling in each
    `interval`-second time bucket. Every chart shares the same x-axis (time) and
    y-axis (sample count) range and ticks, so they can be compared directly. A
    bucket with zero samples is a valid (empty) bar. Returns an HTML fragment
    with inline SVG so the page stays self-contained."""
    if not events:
        return ""
    times = [ts for _, ts in events]
    t0 = min(times)
    duration = max(times) - t0
    # one extra bucket so a sample exactly at the end has a home; at least one
    num_bins = int(duration / interval) + 1

    # bin every thread's samples; remember per-thread totals for ordering
    bins: dict[str, list[int]] = {}
    totals: dict[str, int] = {}
    for name, ts in events:
        b = bins.get(name)
        if b is None:
            b = [0] * num_bins
            bins[name] = b
            totals[name] = 0
        idx = int((ts - t0) / interval)
        if idx >= num_bins:
            idx = num_bins - 1
        b[idx] += 1
        totals[name] += 1
    # busiest thread first, matching the call-tree ordering
    order = sorted(bins, key=lambda n: totals[n], reverse=True)

    global_max = max((max(b) for b in bins.values()), default=0) or 1

    # shared geometry (identical for every chart -> identical scales/ticks)
    left, right, top, bottom = 55, 12, 8, 24
    plot_w, plot_h = 900, 110
    width = left + plot_w + right
    height = top + plot_h + bottom
    x_range = num_bins * interval  # seconds spanned by the bars
    bar_w = plot_w / num_bins

    def ticks(span: float, step: float) -> list[float]:
        out: list[float] = []
        v = 0.0
        while v <= span + step * 0.001:
            out.append(v)
            v += step
        return out

    y_step = _nice_step(global_max, 4)
    y_ticks = ticks(global_max, y_step)
    x_step = _nice_step(x_range, 6)
    x_ticks = ticks(x_range, x_step)

    def y_of(c: float) -> float:
        return top + plot_h - (c / global_max) * plot_h

    def x_of(t: float) -> float:
        return left + (t / x_range) * plot_w

    def chart(name: str) -> str:
        b = bins[name]
        s: list[str] = []
        s.append(
            f'<div class="chart-title">{html.escape(name)} '
            f"({totals[name]} samples)</div>"
        )
        s.append(
            f'<svg width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}">'
        )
        # y grid lines and labels
        for t in y_ticks:
            y = y_of(t)
            s.append(
                f'<line class="grid" x1="{left}" y1="{y:.1f}" '
                f'x2="{left + plot_w}" y2="{y:.1f}"/>'
            )
            s.append(
                f'<text class="tick-label" x="{left - 4}" y="{y + 3:.1f}" '
                f'text-anchor="end">{int(round(t))}</text>'
            )
        # bars
        for i, c in enumerate(b):
            if c <= 0:
                continue
            y = y_of(c)
            s.append(
                f'<rect class="bar" x="{left + i * bar_w:.2f}" y="{y:.2f}" '
                f'width="{bar_w:.2f}" '
                f'height="{top + plot_h - y:.2f}"/>'
            )
        # axes
        s.append(
            f'<line class="axis" x1="{left}" y1="{top + plot_h}" '
            f'x2="{left + plot_w}" y2="{top + plot_h}"/>'
        )
        s.append(
            f'<line class="axis" x1="{left}" y1="{top}" '
            f'x2="{left}" y2="{top + plot_h}"/>'
        )
        # x ticks and labels
        for t in x_ticks:
            x = x_of(t)
            s.append(
                f'<line class="axis" x1="{x:.1f}" y1="{top + plot_h}" '
                f'x2="{x:.1f}" y2="{top + plot_h + 4}"/>'
            )
            s.append(
                f'<text class="tick-label" x="{x:.1f}" '
                f'y="{top + plot_h + 15}" text-anchor="middle">'
                f"{t:g}s</text>"
            )
        s.append("</svg>")
        return "".join(s)

    out = ['<div class="hist">']
    out.append(f"<h2>samples per {interval:g}s interval</h2>")
    for name in order:
        out.append(chart(name))
    out.append("</div>")
    return "\n".join(out)


def gen_html(
    root: Node,
    total: int,
    title: str,
    hist_html: str = "",
    mode: str = "cpu",
) -> str:
    """Render the call-tree HTML.

    `mode` is "cpu" (the default) or "heap":

    - "cpu" weights are perf samples. Percentages at each node are
      computed relative to the top-level frame the node sits under --
      top-level frames are threads, and per-thread percentages make
      sense for CPU profiles because each thread runs independently
      (so every thread root reads 100%).
    - "heap" weights are bytes (from heaptrack -F output). Percentages
      are computed relative to the global total so every node shows
      its share of total peak heap, not its share of one allocator
      callsite's bucket.
    """
    unit = "bytes" if mode == "heap" else "samples"
    parts: list[str] = []
    parts.append("<!doctype html>")
    parts.append('<html><head><meta charset="utf-8">')
    parts.append(f"<title>{html.escape(title)}</title>")
    parts.append(f"<style>{PAGE_CSS}</style>")
    parts.append("</head><body>")
    parts.append(f"<h1>{html.escape(title)}</h1>")
    parts.append(
        f"<p>{total} {unit}. "
        "Navigate with the arrow keys: up/down move, right expands, "
        "left collapses.</p>"
    )
    if hist_html:
        parts.append(hist_html)
    parts.append('<ul class="tree">')
    # For CPU mode each top-level frame is a thread; render its
    # percentages relative to that thread's own count. For heap mode
    # there's only one logical "thread" (allocations) and we want every
    # callsite's share of the global total instead.
    denominator_fn = (lambda c: total) if mode == "heap" else (lambda c: c.count)
    for child in sorted_children(root):
        render_node(child, denominator_fn(child), parts)
    parts.append("</ul>")
    parts.append(f"<script>{PAGE_JS}</script>")
    parts.append("</body></html>")
    return "\n".join(parts)


def main(
    perf_data: Path,
    output: Path,
    threshold: float,
    interval: float = 0.25,
    strip_ret: bool = True,
    collapse_tmpl: bool = True,
    abbrev_ns: bool = True,
) -> None:
    text = run_perf_script(perf_data)
    samples, events = parse_samples(text, strip_ret, collapse_tmpl, abbrev_ns)
    if not samples:
        raise SystemExit(
            f"ERROR: no call-stack samples found in {perf_data}.\n"
            "  was it recorded with call graphs, e.g."
            " 'perf record --call-graph dwarf'?"
        )
    hist_html = gen_histograms(events, interval)
    root = build_tree(samples)
    total = root.count
    prune(root, total * threshold / 100.0)
    page = gen_html(root, total, f"call tree: {perf_data.name}", hist_html)
    output.write_text(page, encoding="utf-8")
    print(f"wrote {output} ({total} samples)")


if __name__ == "__main__":
    p = ArgumentParser(description="generate an HTML call tree from a perf.data file")
    p.add_argument("perf_data", type=Path, help="the perf.data file to process")
    p.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("perf-call-tree.html"),
        help="output HTML file (default: perf-call-tree.html)",
    )
    p.add_argument(
        "--threshold",
        type=float,
        default=0.1,
        help="prune subtrees below this percent of total samples" " (default: 0.1)",
    )
    p.add_argument(
        "--interval",
        type=float,
        default=0.25,
        help="width in seconds of each bar in the per-thread sample-rate"
        " histograms (default: 0.25)",
    )
    p.add_argument(
        "--raw-symbols",
        action="store_true",
        help="keep the leading return type on demangled C++ template"
        " signatures (by default it is stripped)",
    )
    p.add_argument(
        "--full-templates",
        action="store_true",
        help="keep full template argument lists in symbol names (by default"
        " each top-level '<...>' is collapsed for readability)",
    )
    p.add_argument(
        "--full-namespaces",
        action="store_true",
        help="keep the full 'libtorrent::' namespace prefix (by default it is"
        " abbreviated to 'lt::')",
    )
    args = p.parse_args()
    main(
        args.perf_data,
        args.output,
        args.threshold,
        args.interval,
        not args.raw_symbols,
        not args.full_templates,
        not args.full_namespaces,
    )
