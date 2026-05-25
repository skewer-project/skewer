"""
MkDocs hook to pre-render Mermaid diagrams to inline SVGs.

This hook converts mermaid code blocks to static SVG images at build time
using npx @mermaid-js/mermaid-cli. The resulting SVGs are inlined
directly into the HTML, so no JavaScript execution is needed at view time.
"""

import hashlib
import os
import subprocess
import tempfile
from pathlib import Path

from bs4 import BeautifulSoup

CACHE_DIR = Path(".cache/mermaid")


def _ensure_cache_dir():
    """Create cache directory if it doesn't exist."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)


def _hash_source(source: str) -> str:
    """Compute a SHA256 hash of the mermaid source for caching."""
    return hashlib.sha256(source.encode("utf-8")).hexdigest()


def _render_svg(source: str, cache_key: str) -> str:
    """Render mermaid source to SVG using npx mermaid-cli."""
    _ensure_cache_dir()
    cache_path = CACHE_DIR / f"{cache_key}.svg"

    if cache_path.exists():
        return cache_path.read_text("utf-8")

    # Write source to temp file
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".mmd", delete=False, encoding="utf-8"
    ) as f:
        f.write(source)
        temp_input = f.name

    try:
        # Run mermaid-cli via npx
        subprocess.run(
            [
                "npx",
                "-y",
                "@mermaid-js/mermaid-cli",
                "-i",
                temp_input,
                "-o",
                str(cache_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"[mermaid_svg] Failed to render diagram: {e.stderr}")
        return None
    finally:
        os.unlink(temp_input)

    if cache_path.exists():
        return cache_path.read_text("utf-8")
    return None


def on_page_content(html: str, page, config, files) -> str:
    """Convert mermaid code blocks to inline SVGs."""
    soup = BeautifulSoup(html, "html.parser")

    # Find mermaid code blocks. Material can produce either:
    #   <pre class="mermaid"><code>...</code></pre>   (custom fence)
    #   <pre><code class="language-mermaid">...</code></pre>  (default fence)
    blocks = (
        soup.select("pre.mermaid code")
        or soup.select("pre code.language-mermaid")
    )

    if not blocks:
        return html

    for block in blocks:
        source = block.get_text()
        if not source.strip():
            continue

        cache_key = _hash_source(source)
        svg_content = _render_svg(source, cache_key)

        if svg_content:
            # Parse the SVG and wrap it in a constrained container
            # so Paged.js doesn't choke on wide viewBox dimensions.
            svg_soup = BeautifulSoup(svg_content, "html.parser")
            svg_tag = svg_soup.find("svg")
            if svg_tag:
                wrapper = soup.new_tag("div")
                wrapper["class"] = "mermaid-diagram"
                svg_tag.wrap(wrapper)

                block.parent.replace_with(wrapper)
        else:
            # Leave the code block as-is if rendering failed
            print(f"[mermaid_svg] Warning: leaving unrendered mermaid block on {page.url}")

    return str(soup)
