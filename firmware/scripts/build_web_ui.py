# pyright: reportMissingImports=false, reportUndefinedVariable=false

Import("env")

from pathlib import Path
import gzip
import re
import sys

try:
    import minify_html
except ImportError as exc:
    sys.stderr.write(
        "[build-ui] Missing dependency: minify-html\n"
        "[build-ui] Install it with: python3 -m pip install -r firmware/requirements-ui.txt\n"
    )
    raise SystemExit(1) from exc

PROJECT_DIR = Path(env["PROJECT_DIR"])
UI_DIR      = PROJECT_DIR / "ui"
DATA_DIR    = PROJECT_DIR / "data"

STYLE_RE  = re.compile(r'<link\s+rel=["\']stylesheet["\']\s+href=["\']([^"\']+)["\']\s*/?>', re.IGNORECASE)
SCRIPT_RE = re.compile(r'<script\s+src=["\']([^"\']+)["\']\s*></script>', re.IGNORECASE)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def inline_assets(html: str, source_html: Path) -> str:
    """
    Collect all local <link rel="stylesheet"> files, concatenate them, and
    replace ALL link tags with a single <style> block.
    Collect all local <script src="..."> files, concatenate them, and
    replace ALL script tags with a single <script> block.
    This keeps functions in a shared scope (no cross-block execution order issues).
    """
    # ── CSS ────────────────────────────────────────────────────────────────
    css_parts = []

    def collect_css(match):
        href = match.group(1)
        if href.startswith(("http://", "https://", "//")):
            return match.group(0)          # leave external links alone
        css_path = (source_html.parent / href).resolve()
        css_parts.append(read_text(css_path))
        return ""                           # remove this link tag

    html = STYLE_RE.sub(collect_css, html)

    if css_parts:
        combined_css = "\n".join(css_parts)
        # Insert the combined <style> just before </head>
        html = html.replace("</head>", f"<style>{combined_css}</style>\n</head>", 1)

    # ── JS ─────────────────────────────────────────────────────────────────
    js_parts = []

    def collect_js(match):
        src = match.group(1)
        if src.startswith(("http://", "https://", "//")):
            return match.group(0)           # leave external scripts alone
        js_path = (source_html.parent / src).resolve()
        js_parts.append(read_text(js_path))
        return ""                           # remove this script tag

    html = SCRIPT_RE.sub(collect_js, html)

    if js_parts:
        combined_js = "\n".join(js_parts)
        # Insert the combined <script> just before </body>
        html = html.replace("</body>", f"<script>{combined_js}</script>\n</body>", 1)

    return html


def write_if_changed(path: Path, content: bytes):
    if path.exists() and path.read_bytes() == content:
        return False
    path.write_bytes(content)
    return True


def build_one(source_html: Path, output_stem: str):
    """Build one HTML file → <output_stem>.html + <output_stem>.html.gz in DATA_DIR."""
    if not source_html.exists():
        sys.stderr.write(f"[build-ui] Missing UI source: {source_html}\n")
        raise SystemExit(1)

    DATA_DIR.mkdir(parents=True, exist_ok=True)

    html = read_text(source_html)
    html = inline_assets(html, source_html)
    html = minify_html.minify(
        html,
        minify_js=True,
        minify_css=True,
        keep_comments=False,
    )

    output_html = DATA_DIR / f"{output_stem}.html"
    output_gzip = DATA_DIR / f"{output_stem}.html.gz"

    gzip_bytes = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)

    html_changed = write_if_changed(output_html, html.encode("utf-8"))
    gzip_changed = write_if_changed(output_gzip, gzip_bytes)

    status = "updated" if html_changed or gzip_changed else "up to date"
    print(
        f"[build-ui] {output_stem}: {status} "
        f"({len(html)} bytes HTML, {len(gzip_bytes)} bytes gzip)"
    )


def build_web_ui(*_args, **_kwargs):
    build_one(UI_DIR / "index.html",  "index")
    build_one(UI_DIR / "wizard.html", "wizard")


# Run unconditionally, every invocation — this script is loaded via `pre:` in
# platformio.ini, which runs regardless of target (build/buildfs/upload/...).
# Gating this behind env.AddPreAction("$BUILD_DIR/littlefs.bin", ...) instead
# only fired when SCons already considered that target out-of-date — a
# decision based on the `data/` output directory, which nothing here declares
# as depending on `ui/`. Editing ui/*.js and running `-t buildfs` would then
# silently skip regeneration and flash whatever was already in data/.
# write_if_changed() already no-ops when content is identical, so calling
# this every time is cheap.
build_web_ui()
