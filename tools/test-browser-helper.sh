#!/bin/sh
set -eu

command -v python3 >/dev/null 2>&1 || exit 77
export GPCLIENT_TOP_SRCDIR=${GPCLIENT_TOP_SRCDIR:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}

python3 - <<'PY'
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

helper = Path(os.environ["GPCLIENT_TOP_SRCDIR"]) / "src/nm-gpclient-browser-helper"

with tempfile.TemporaryDirectory(prefix="gpclient-browser-") as directory:
    root = Path(directory)
    binaries = root / "bin"
    binaries.mkdir()
    for name, script in {
        "gdbus": 'printf "%s\\n" "$@" > "$FAKE_BROWSER_ARGS"',
        "sleep": ': > "$FAKE_SLEEP"; exit 1',
        "custom-browser": 'printf "%s\\n" "$@" > "$FAKE_BROWSER_ARGS"',
    }.items():
        path = binaries / name
        path.write_text("#!/bin/sh\n" + script + "\n")
        path.chmod(0o755)

    bus = socket.socket(socket.AF_UNIX)
    bus.bind(str(root / "bus"))
    env = dict(os.environ)
    env.update(
        PATH=str(binaries) + ":" + env.get("PATH", "/usr/bin:/bin"),
        XDG_RUNTIME_DIR=str(root),
        NM_GPCLIENT_BROWSER="default",
        FAKE_BROWSER_ARGS=str(root / "args"),
        FAKE_SLEEP=str(root / "slept"),
    )
    env.pop("NM_GPCLIENT_BROWSER_LOG", None)
    url = "https://example.invalid/auth?test-token=synthetic"

    def launch():
        (root / "args").unlink(missing_ok=True)
        result = subprocess.run([str(helper), url], env=env, capture_output=True, timeout=3)
        assert result.returncode == 0, result
        assert not result.stdout and not result.stderr, result
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if (root / "args").exists() and url in (root / "args").read_text():
                return (root / "args").read_text().splitlines()
            time.sleep(0.01)
        raise AssertionError("browser was not launched")

    # Upstream gpauth is ready before invoking the helper; no port file or delay.
    arguments = launch()
    assert "org.freedesktop.portal.OpenURI.OpenURI" in arguments, arguments
    assert not (root / "slept").exists()
    assert not (root / "gpcallback.port").exists()

    # Packaged browser commands resolve through the inherited PATH.
    env["NM_GPCLIENT_BROWSER"] = "custom-browser"
    assert launch() == [url]

    # Diagnostics require explicit opt-in.
    env["NM_GPCLIENT_BROWSER_LOG"] = str(root / "debug.log")
    launch()
    assert "browser=custom-browser" in (root / "debug.log").read_text()

    for browser in ("manual", "remote"):
        env["NM_GPCLIENT_BROWSER"] = browser
        result = subprocess.run([str(helper), url], env=env, capture_output=True, timeout=3)
        assert result.returncode == 2, result
    bus.close()

print("browser-helper tests passed.")
PY
