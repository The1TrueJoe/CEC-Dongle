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
UI_DIR = PROJECT_DIR / "ui"
DATA_DIR = PROJECT_DIR / "data"
SOURCE_HTML = UI_DIR / "index.html"
OUTPUT_HTML = DATA_DIR / "index.html"
OUTPUT_GZIP = DATA_DIR / "index.html.gz"

STYLE_RE = re.compile(r'<link\s+rel=["\']stylesheet["\']\s+href=["\']([^"\']+)["\']\s*/?>', re.IGNORECASE)
SCRIPT_RE = re.compile(r'<script\s+src=["\']([^"\']+)["\']\s*></script>', re.IGNORECASE)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def inline_assets(html: str) -> str:
    def replace_style(match):
        href = match.group(1)
        if href.startswith(("http://", "https://", "//")):
            return match.group(0)
        css_path = (SOURCE_HTML.parent / href).resolve()
        css = read_text(css_path)
        return f"<style>\n{css}\n</style>"

    def replace_script(match):
        src = match.group(1)
        if src.startswith(("http://", "https://", "//")):
            return match.group(0)
        js_path = (SOURCE_HTML.parent / src).resolve()
        js = read_text(js_path)
        return f"<script>\n{js}\n</script>"

    html = STYLE_RE.sub(replace_style, html)
    html = SCRIPT_RE.sub(replace_script, html)
    return html


def write_if_changed(path: Path, content: bytes):
    if path.exists() and path.read_bytes() == content:
        return False
    path.write_bytes(content)
    return True


def build_web_ui(*_args, **_kwargs):
    if not SOURCE_HTML.exists():
        sys.stderr.write(f"[build-ui] Missing UI source: {SOURCE_HTML}\n")
        raise SystemExit(1)

    DATA_DIR.mkdir(parents=True, exist_ok=True)

    html = read_text(SOURCE_HTML)
    html = inline_assets(html)
    html = minify_html.minify(
        html,
        minify_js=True,
        minify_css=True,
        keep_comments=False,
    )

    gzip_bytes = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)

    html_changed = write_if_changed(OUTPUT_HTML, html.encode("utf-8"))
    gzip_changed = write_if_changed(OUTPUT_GZIP, gzip_bytes)

    status = "updated" if html_changed or gzip_changed else "up to date"
    print(
        f"[build-ui] {status}: {OUTPUT_HTML.name} ({len(html)} bytes), "
        f"{OUTPUT_GZIP.name} ({len(gzip_bytes)} bytes)"
    )


env.AddPreAction("$BUILD_DIR/littlefs.bin", build_web_ui)
