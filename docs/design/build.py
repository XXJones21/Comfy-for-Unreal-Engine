"""Build comfy-for-unreal.html from the template, inlining images from img/ as data URIs.

Usage: python build.py
"""

import base64
import mimetypes
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEMPLATE = HERE / "comfy-for-unreal.template.html"
OUTPUT = HERE / "comfy-for-unreal.html"


def data_uri(name: str) -> str:
    path = HERE / "img" / name
    mime = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
    return f"data:{mime};base64,{base64.b64encode(path.read_bytes()).decode('ascii')}"


def main() -> None:
    html = TEMPLATE.read_text(encoding="utf-8")
    cache: dict[str, str] = {}
    html = re.sub(r"\{\{img:([^}]+)\}\}", lambda m: cache.setdefault(m[1], data_uri(m[1])), html)
    OUTPUT.write_text(html, encoding="utf-8")
    print(f"Wrote {OUTPUT.name} ({OUTPUT.stat().st_size / 1024:.0f} KB, {len(cache)} images)")


if __name__ == "__main__":
    main()
