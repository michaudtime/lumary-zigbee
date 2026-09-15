#!/usr/bin/env python3
"""Serves tools/designer so the ring designer can be opened in a browser.

The page is ES modules, and browsers refuse to load those over file:// -- so it
needs to be served rather than double-clicked. This is a plain static server and
nothing more: it does not talk to the light. Pushing effects to the fixture and
reading back what is in its slots is the local helper's job, and that is a
separate change.

Usage:  python scripts/serve-designer.py [port]
Then open the URL it prints.
"""

import functools
import http.server
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "tools" / "designer"


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8730
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(ROOT))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as httpd:
        print(f"Ring designer at http://127.0.0.1:{port}/  (Ctrl-C to stop)")
        print(f"serving {ROOT}")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
