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
from pathlib import Path
import re
import subprocess
from typing import Optional


# a node in the calling-context tree. "count" is the number of samples whose
# stack passes through this node along this exact path (inclusive count).
class Node:
    __slots__ = ("name", "count", "children")

    def __init__(self, name: str) -> None:
        self.name = name
        self.count = 0
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
# and let comm absorb everything before it. the cpu field is optional.
_HEADER_RE = re.compile(
    r"^(?P<comm>.+?)\s+(?P<pid>\d+)(?:/(?P<tid>\d+))?\s+" r"(?:\[\d+\]\s+)?[\d.]+:"
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
) -> list[list[str]]:
    """Parse 'perf script' output into a list of call stacks. Each stack is a
    list of function names ordered root -> leaf. A synthetic frame identifying
    the originating thread (comm + tid) is prepended to every stack, so each
    thread gets its own root in the resulting tree."""
    samples: list[list[str]] = []
    frames: list[str] = []
    have_record = False
    thread: Optional[str] = None

    def flush() -> None:
        nonlocal frames, have_record
        if have_record and frames:
            # perf prints leaf first; reverse to root -> leaf
            frames.reverse()
            frames.insert(0, thread if thread is not None else "[unknown thread]")
            samples.append(frames)
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
            # the thread identity this record's frames belong to.
            flush()
            have_record = True
            hm = _HEADER_RE.match(line)
            if hm is not None:
                tid = hm.group("tid") or hm.group("pid")
                thread = f"{hm.group('comm').strip()} (tid {tid})"
            else:
                thread = "[unknown thread]"
            continue
        frames.append(
            frame_symbol(m.group(1), m.group(2), strip_ret, collapse_tmpl, abbrev_ns)
        )
    flush()
    return samples


def build_tree(samples: list[list[str]]) -> Node:
    """Fold call stacks into a calling-context tree with inclusive counts."""
    root = Node("[root]")
    for stack in samples:
        root.count += 1
        node = root
        for name in stack:
            node = node.child(name)
            node.count += 1
    return root


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
"""

PAGE_JS = """
(function () {
  var nodes = Array.prototype.slice.call(document.querySelectorAll('.node'));
  var selected = null;

  function liOf(node) { return node.parentNode; }
  function hasChildren(node) { return liOf(node).classList.contains('has-children'); }
  function isExpanded(node) { return liOf(node).classList.contains('expanded'); }

  function childList(node) { return liOf(node).querySelector(':scope > ul.children'); }

  function expand(node) {
    if (!hasChildren(node)) return;
    liOf(node).classList.add('expanded');
    var ul = childList(node);
    ul.hidden = false;
    // collapse degenerate chains: if there is exactly one child, expand it
    // too, recursively, until we reach a node with 0 or more than 1 children
    var childLis = ul.querySelectorAll(':scope > li');
    if (childLis.length === 1) {
      expand(childLis[0].querySelector(':scope > .node'));
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


def gen_html(root: Node, total: int, title: str) -> str:
    parts: list[str] = []
    parts.append("<!doctype html>")
    parts.append('<html><head><meta charset="utf-8">')
    parts.append(f"<title>{html.escape(title)}</title>")
    parts.append(f"<style>{PAGE_CSS}</style>")
    parts.append("</head><body>")
    parts.append(f"<h1>{html.escape(title)}</h1>")
    parts.append(
        f"<p>{total} samples. "
        "Navigate with the arrow keys: up/down move, right expands, "
        "left collapses.</p>"
    )
    parts.append('<ul class="tree">')
    # render the real top-level frames, not the synthetic root. each top-level
    # node is a thread, so percentages are computed relative to that thread's
    # own sample count -- every thread root reads 100%.
    for child in sorted_children(root):
        render_node(child, child.count, parts)
    parts.append("</ul>")
    parts.append(f"<script>{PAGE_JS}</script>")
    parts.append("</body></html>")
    return "\n".join(parts)


def main(
    perf_data: Path,
    output: Path,
    threshold: float,
    strip_ret: bool = True,
    collapse_tmpl: bool = True,
    abbrev_ns: bool = True,
) -> None:
    text = run_perf_script(perf_data)
    samples = parse_samples(text, strip_ret, collapse_tmpl, abbrev_ns)
    if not samples:
        raise SystemExit(
            f"ERROR: no call-stack samples found in {perf_data}.\n"
            "  was it recorded with call graphs, e.g."
            " 'perf record --call-graph dwarf'?"
        )
    root = build_tree(samples)
    total = root.count
    prune(root, total * threshold / 100.0)
    page = gen_html(root, total, f"call tree: {perf_data.name}")
    output.write_text(page)
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
        not args.raw_symbols,
        not args.full_templates,
        not args.full_namespaces,
    )
