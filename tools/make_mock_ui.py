#!/usr/bin/env python3
"""Build mock/index.html — the web UI with a simulated box behind it.

Lifts the kIndexHtml raw string straight out of src/WebUi.cpp and injects
tools/mock_backend.js ahead of the page's own <script>, so the resulting file
is the firmware's UI, unmodified, talking to an in-memory ESP32. Open it in a
browser (no server needed) to demo or screen-record the controls with no
hardware present.

Re-run it after any change to the UI in WebUi.cpp:

    python3 tools/make_mock_ui.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEBUI = ROOT / "src" / "WebUi.cpp"
BACKEND = ROOT / "tools" / "mock_backend.js"
OUT = ROOT / "mock" / "index.html"

BANNER = """
<!-- ===================================================================
     GENERATED FILE — do not edit.

     The page below is the Slow Arc Controller UI, copied verbatim from
     src/WebUi.cpp. The script that follows replaces window.fetch with a
     simulated controller so the whole UI works with no hardware.

     Rebuild with:  python3 tools/make_mock_ui.py
     ================================================================== -->
"""


def extract_page(source: str) -> str:
    """Return the kIndexHtml raw-string payload from WebUi.cpp."""
    m = re.search(r'kIndexHtml\[\]\s*PROGMEM\s*=\s*R"HTML\((.*?)\)HTML";',
                  source, re.DOTALL)
    if not m:
        sys.exit("error: could not find the kIndexHtml R\"HTML(...)\" literal in "
                 f"{WEBUI.relative_to(ROOT)} — has the UI been restructured?")
    return m.group(1).strip()


def main() -> None:
    page = extract_page(WEBUI.read_text(encoding="utf-8"))
    backend = BACKEND.read_text(encoding="utf-8")

    # The shim has to be installed before the page's script runs its first poll().
    marker = "<script>"
    if page.count(marker) != 1:
        sys.exit(f"error: expected exactly one {marker} in the page, "
                 f"found {page.count(marker)}")
    page = page.replace(marker, f"<script>\n{backend}</script>\n{marker}", 1)

    # A closing </script> inside the injected JS would end the block early. None
    # is expected, but a future edit could add one.
    if "</script>" in backend:
        sys.exit("error: mock_backend.js contains a literal </script>")

    head = "<head>"
    page = page.replace(head, head + BANNER, 1)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(page + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}  ({OUT.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
