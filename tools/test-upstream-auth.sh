#!/bin/sh
# Exercise real upstream binaries without a VPN server or browser SSO session.
set -eu
command -v python3 >/dev/null 2>&1 || exit 77
export GPCLIENT_TOP_SRCDIR=${GPCLIENT_TOP_SRCDIR:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}

python3 - "$@" <<'PY'
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

if len(sys.argv) not in (1, 3):
    sys.exit("Usage: test-upstream-auth.sh [GPCLIENT GPAUTH]")
paths = sys.argv[1:] or [shutil.which("gpclient"), shutil.which("gpauth")]
if not all(paths):
    sys.exit("gpclient and gpauth 3.x are required")
client, auth = [str(Path(path).resolve()) for path in paths]
helper = Path(os.environ["GPCLIENT_TOP_SRCDIR"]) / "src/nm-gpclient-browser-helper"
for binary, name in ((client, "gpclient"), (auth, "gpauth")):
    version = subprocess.check_output([binary, "--version"], text=True)
    if not version.startswith(name + " 3."):
        sys.exit(name + " 3.x is required")

with tempfile.TemporaryDirectory(prefix="gpclient3-auth-") as directory:
    root = Path(directory)
    binaries = root / "bin"
    binaries.mkdir()
    home = root / "home"
    home.mkdir()
    # Avoid changing the real user's MIME handler during this isolated test.
    mime = binaries / "xdg-mime"
    mime.write_text('#!/bin/sh\nprintf "%s\\n" gpgui.desktop\n')
    mime.chmod(0o755)
    browser = binaries / "test-browser"
    browser.write_text('''#!/usr/bin/env python3
import os
import subprocess
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    assert response.read() == b'<html>synthetic SAML request</html>'
subprocess.run([os.environ['TEST_GPCLIENT'], 'launch-gui', os.environ['TEST_CALLBACK']], check=True, timeout=10)
''')
    browser.chmod(0o755)
    env = dict(os.environ, HOME=str(home), PATH=str(binaries) + ":" + os.environ.get("PATH", "/usr/bin:/bin"))
    env.update(TEST_GPCLIENT=client, GP_AUTH_BINARY=auth, NM_GPCLIENT_BROWSER=str(browser))
    env.pop("NM_GPCLIENT_BROWSER_LOG", None)
    env.pop("XDG_CURRENT_DESKTOP", None)
    outcomes = [
        ("globalprotectcallback:cas-as=1&un=callback-test@example.invalid&token=synthetic-test-token", "success"),
        ("globalprotectcallback:" + base64.b64encode(b'<response><saml-auth-status>0</saml-auth-status></response>').decode(), "failure"),
    ]
    for callback, expected in outcomes:
        env["TEST_CALLBACK"] = callback
        result = subprocess.run([
            auth, "gateway.example.invalid", "--gateway", "--host-id", "nm-integration-host",
            "--saml-request", "<html>synthetic SAML request</html>", "--browser", str(helper),
        ], env=env, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stderr
        data = json.loads(result.stdout)
        assert expected in data, data
        if expected == "success":
            assert data["success"]["username"] == "callback-test@example.invalid"
            assert data["success"]["token"] == "synthetic-test-token"
            assert data["success"]["hostId"] == "nm-integration-host"
        print("PASS real gpauth -> NM browser helper -> browser -> gpclient launch-gui -> gpauth auth-callback: " + expected)
PY
