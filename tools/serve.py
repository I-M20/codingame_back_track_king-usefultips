#!/usr/bin/env python3
"""Serve the debug viewer and the turn dumps written by tools/btk-debug.

The dumps are plain JSON files, so the only thing this adds over a static file
server is an index of which turns exist -- and re-reading it on every request,
so a live match shows up in the viewer as it is played.

    make viewer                  # defaults: tools/dumps on :8000
    python3 tools/serve.py --dumps some/dir --port 9000
"""

import argparse
import json
import re
import socket
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
TURN_RE = re.compile(r"^turn_(\d+)\.json$")


def turn_numbers(dumps: Path) -> list[int]:
    if not dumps.is_dir():
        return []
    found = (TURN_RE.match(p.name) for p in dumps.iterdir())
    return sorted(int(m.group(1)) for m in found if m)


class Handler(SimpleHTTPRequestHandler):
    dumps = TOOLS_DIR / "dumps"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(TOOLS_DIR), **kwargs)

    def do_GET(self):
        try:
            if self.path in ("/", "/index.html"):
                self.path = "/viewer.html"
            elif self.path == "/api/turns":
                return self._json({"turns": turn_numbers(self.dumps)})
            elif self.path.startswith("/api/turn/"):
                return self._turn(self.path.rsplit("/", 1)[-1])
            return super().do_GET()
        except (BrokenPipeError, ConnectionResetError):
            # The browser hung up mid-response -- it navigated away, reloaded,
            # or cancelled a poll. There is nobody left to answer, and the
            # default handler would print a traceback for a non-event.
            pass

    def _turn(self, raw: str):
        try:
            number = int(raw)
        except ValueError:
            return self.send_error(400, "turn must be a number")
        path = self.dumps / f"turn_{number:03d}.json"
        if not path.is_file():
            return self.send_error(404, f"no dump for turn {number}")
        # Served as raw bytes: the file is already JSON, and a live match may
        # be mid-write, in which case the browser reports the parse error and
        # retries rather than the server holding the turn.
        body = path.read_bytes()
        self._send(body, "application/json")

    def _json(self, payload):
        self._send(json.dumps(payload).encode(), "application/json")

    def _send(self, body: bytes, content_type: str):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        # Polling /api/turns every couple of seconds in live mode would bury
        # anything worth reading. Errors come through log_error, whose args are
        # not the request line, so they are formatted before being matched.
        line = fmt % args
        if "/api/" not in line:
            super().log_message("%s", line)


def wsl_address() -> str | None:
    """The distro's IP as seen from Windows, or None when not under WSL."""
    try:
        if "microsoft" not in Path("/proc/sys/kernel/osrelease").read_text().lower():
            return None
    except OSError:
        return None
    # No route is actually opened: connect() on a UDP socket only picks the
    # interface the kernel would use, which is the address Windows can reach.
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))
            return s.getsockname()[0]
    except OSError:
        return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dumps", default=str(TOOLS_DIR / "dumps"),
                    help="directory holding turn_NNN.json (default tools/dumps)")
    ap.add_argument("--port", type=int, default=8000)
    args = ap.parse_args()

    Handler.dumps = Path(args.dumps).resolve()
    turns = turn_numbers(Handler.dumps)

    # Flushed as they are printed: these lines carry the URL to open, and a
    # pipe or an editor's terminal would otherwise hold them until exit.
    say = lambda msg: print(msg, flush=True)

    if turns:
        say(f"{len(turns)} turns in {Handler.dumps} "
            f"({turns[0]}..{turns[-1]})")
    else:
        say(f"no dumps yet in {Handler.dumps} -- run `make debug` then "
            f"btk-debug replay/live")

    say(f"viewer on http://localhost:{args.port}/")
    # Under WSL, a browser running on Windows does not reach the distro's
    # localhost unless the port happens to be forwarded, so the address that
    # does work is printed alongside it.
    host_ip = wsl_address()
    if host_ip:
        say(f"  from Windows (WSL): http://{host_ip}:{args.port}/")

    try:
        HTTPServer(("", args.port), Handler).serve_forever()
    except KeyboardInterrupt:
        print()
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
