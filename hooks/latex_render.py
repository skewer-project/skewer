"""
MkDocs hook to pre-render LaTeX math to static HTML via KaTeX.

This hook finds arithmatex blocks (produced by pymdownx.arithmatex with
generic mode) and replaces them with server-side rendered KaTeX output,
so math renders in both web and PDF builds without JavaScript.
"""

import hashlib
import os
import re
import subprocess
from pathlib import Path

from bs4 import BeautifulSoup

CACHE_DIR = Path(".cache/katex")

DISPLAY_DELIMS_RE = re.compile(r"^\\\[|\\\]$")
INLINE_DELIMS_RE = re.compile(r"^\\\(|\\\)$")

# Inline JS renderer — passes LaTeX via stdin, writes KaTeX HTML to stdout
KAETEX_RENDERER = r"""
const katex = require("katex");
let input = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (c) => (input += c));
process.stdin.on("end", () => {
  const dm = process.env.DISPLAY_MODE === "1";
  try {
    process.stdout.write(
      katex.renderToString(input.trim(), {
        displayMode: dm,
        throwOnError: false,
        strict: false,
      })
    );
  } catch (e) {
    process.stderr.write("[katex] " + e.message + "\n");
    process.stdout.write(input);
  }
});
"""


def _ensure_cache_dir():
    CACHE_DIR.mkdir(parents=True, exist_ok=True)


def _hash_source(source: str, display: bool) -> str:
    raw = f"{'d' if display else 'i'}:{source}"
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def _katex_node_path() -> str | None:
    """Find the node_modules dir that contains katex in the npx cache."""
    npx_cache = Path.home() / ".npm" / "_npx"
    if not npx_cache.exists():
        return None

    for pkg_json in npx_cache.glob("*/node_modules/katex/package.json"):
        # Return the node_modules dir (parent of the katex dir)
        return str(pkg_json.parent.parent)

    return None


def _ensure_katex_installed() -> str | None:
    """Make sure katex is cached via npx, return its node_modules path."""
    node_path = _katex_node_path()
    if node_path:
        return node_path

    # Trigger npx to install katex into its cache
    subprocess.run(
        ["npx", "-y", "-p", "katex", "node", "-e", ""],
        capture_output=True,
    )
    return _katex_node_path()


def _render_katex(source: str, display: bool, cache_key: str) -> str | None:
    """Render LaTeX to HTML via KaTeX (node + NODE_PATH)."""
    _ensure_cache_dir()
    cache_path = CACHE_DIR / f"{cache_key}.html"

    if cache_path.exists():
        return cache_path.read_text("utf-8")

    node_path = _ensure_katex_installed()
    env = {**os.environ, "DISPLAY_MODE": "1" if display else "0"}
    if node_path:
        env["NODE_PATH"] = node_path

    try:
        result = subprocess.run(
            ["node", "-e", KAETEX_RENDERER],
            input=source,
            capture_output=True,
            text=True,
            env=env,
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"[latex_render] Failed to render formula: {e.stderr}")
        return None

    html = result.stdout.strip()
    if html:
        cache_path.write_text(html, "utf-8")
    return html or None


def on_page_content(html: str, page, config, files) -> str:
    """Convert arithmatex blocks to pre-rendered KaTeX HTML."""
    soup = BeautifulSoup(html, "html.parser")

    elements = soup.find_all(class_="arithmatex")
    if not elements:
        return html

    for el in elements:
        is_display = el.name == "div"
        raw_text = el.get_text()

        # Strip arithmatex wrapper delimiters
        source = raw_text.strip()
        if is_display:
            source = DISPLAY_DELIMS_RE.sub("", source).strip()
        else:
            source = INLINE_DELIMS_RE.sub("", source).strip()

        if not source:
            continue

        cache_key = _hash_source(source, is_display)
        rendered = _render_katex(source, is_display, cache_key)

        if rendered:
            replacement = BeautifulSoup(rendered, "html.parser")
            el.replace_with(replacement)
        else:
            print(f"[latex_render] Warning: unrendered math on {page.url}")

    return str(soup)
