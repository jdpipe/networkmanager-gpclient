#!/bin/sh
set -eu

command -v python3 >/dev/null 2>&1 || exit 77
export GPCLIENT_TOP_SRCDIR=${GPCLIENT_TOP_SRCDIR:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}

python3 - <<'PY'
import os
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

helper = Path(os.environ["GPCLIENT_TOP_SRCDIR"]) / "src/nm-gpclient-browser-helper"

with tempfile.TemporaryDirectory(prefix="gpclient-browser-") as directory:
    root = Path(directory)
    binaries = root / "bin"
    binaries.mkdir()
    # Record argv, including the executable, so a portal call cannot masquerade
    # as a Firefox launch. Package queries are local, simulated installation checks.
    script = '''import json, os, sys
from pathlib import Path
call = [Path(sys.argv[0]).name, *sys.argv[1:]]
with open(os.environ["FAKE_CALLS"], "a") as output:
    output.write(json.dumps(call) + "\\n")
if call == ["snap", "list", "firefox"]:
    sys.exit(int(os.environ.get("FAKE_SNAP_STATUS", "1")))
if call == ["flatpak", "info", "org.mozilla.firefox"]:
    sys.exit(int(os.environ.get("FAKE_FLATPAK_STATUS", "1")))
if call[0] == "sleep":
    Path(os.environ["FAKE_SLEEP"]).touch()
    sys.exit(1)
Path(os.environ["FAKE_BROWSER_ARGS"]).write_text(json.dumps(call))
sys.exit(int(os.environ.get("FAKE_LAUNCH_STATUS", "0")))
'''
    for name in ("gdbus", "gio", "xdg-open", "firefox", "firefox-esr",
                 "snap", "flatpak", "sleep", "custom-browser",
                 "google-chrome-stable", "google-chrome", "chromium", "microsoft-edge"):
        path = binaries / name
        path.write_text("#!" + sys.executable + "\n" + script)
        path.chmod(0o755)

    # Only the socket's existence is inspected; no D-Bus service is contacted.
    # A socket inode also works in test sandboxes that disallow socket().
    bus = root / "bus"
    os.mknod(bus, stat.S_IFSOCK | 0o600)
    env = dict(os.environ)
    env.update(
        PATH=str(binaries) + ":" + env.get("PATH", "/usr/bin:/bin"),
        XDG_RUNTIME_DIR=str(root),
        NM_GPCLIENT_BROWSER="default",
        FAKE_BROWSER_ARGS=str(root / "args"),
        FAKE_CALLS=str(root / "calls"),
        FAKE_BIN=str(binaries),
        FAKE_SLEEP=str(root / "slept"),
        FAKE_LAUNCH_STATUS="0",
    )
    env.pop("NM_GPCLIENT_BROWSER_LOG", None)
    url = "https://example.invalid/auth?test-token=synthetic&quoted='value'&literal=$(false)%20*"

    # The helper extends PATH with system directories. Mock only command lookup
    # for absence tests, so browsers installed on the test host cannot affect them.
    # Source the unmodified helper in a fresh shell with its normal URL argument.
    isolated_shell = '''
command() {
    [ "$#" -eq 2 ] && [ "$1" = -v ] || return 2
    case " $FAKE_AVAILABLE " in
        *" $2 "*) printf '%s/%s\\n' "$FAKE_BIN" "$2" ;;
        *) return 1 ;;
    esac
}
helper=$1
shift
. "$helper"
'''
    checks = 0

    def launch(expected_status=0, available=None, with_url=True):
        global checks
        checks += 1
        (root / "args").unlink(missing_ok=True)
        (root / "calls").unlink(missing_ok=True)
        argv = [str(helper)]
        if available is not None:
            env["FAKE_AVAILABLE"] = " ".join(available)
            argv = ["/bin/sh", "-c", isolated_shell, str(helper), str(helper)]
        if with_url:
            argv.append(url)
        result = subprocess.run(argv, env=env, capture_output=True, timeout=5)
        assert result.returncode == expected_status, result
        assert not result.stdout and not result.stderr, result
        return json.loads((root / "args").read_text()) if (root / "args").exists() else None

    def calls():
        return [json.loads(line) for line in (root / "calls").read_text().splitlines()] if (root / "calls").exists() else []

    # Upstream gpauth is ready before invoking the helper; no port file or delay.
    arguments = launch()
    portal = ["gdbus", "call", "--session", "--dest", "org.freedesktop.portal.Desktop",
              "--object-path", "/org/freedesktop/portal/desktop",
              "--method", "org.freedesktop.portal.OpenURI.OpenURI", "", url, "{}"]
    assert arguments == portal, arguments
    assert not (root / "slept").exists()
    assert not (root / "gpcallback.port").exists()

    # Exercise the real PATH lookup as well as the isolated fallback matrix.
    # A native Firefox or distro/Snap wrapper on PATH must beat the portal.
    env["NM_GPCLIENT_BROWSER"] = "firefox"
    arguments = launch()
    assert arguments == ["firefox", url], arguments
    assert calls() == [["firefox", url]]

    for has_bus in (True, False):
        if not has_bus:
            bus.unlink()
        for has_gdbus in (True, False):
            desktop = ["gio", "xdg-open"] + (["gdbus"] if has_gdbus else [])
            env["FAKE_SNAP_STATUS"] = "0"
            env["FAKE_FLATPAK_STATUS"] = "0"
            for available, expected, probes in (
                (["firefox", "firefox-esr", "snap", "flatpak"], ["firefox", url], []),
                (["firefox-esr", "snap", "flatpak"], ["firefox-esr", url], []),
                (["snap", "flatpak"], ["snap", "run", "firefox", url], [["snap", "list", "firefox"]]),
                (["flatpak"], ["flatpak", "run", "org.mozilla.firefox", url], [["flatpak", "info", "org.mozilla.firefox"]]),
            ):
                assert launch(available=desktop + available) == expected
                assert calls() == probes + [expected], calls()
                # Launch failures must not try a different profile or the portal.
                env["FAKE_LAUNCH_STATUS"] = "23"
                assert launch(expected_status=23, available=desktop + available) == expected
                assert calls() == probes + [expected], calls()
                env["FAKE_LAUNCH_STATUS"] = "0"

            # Package-manager presence is not proof that Firefox is installed.
            env["FAKE_SNAP_STATUS"] = "1"
            flatpak = ["flatpak", "run", "org.mozilla.firefox", url]
            assert launch(available=desktop + ["snap", "flatpak"]) == flatpak
            assert calls() == [["snap", "list", "firefox"], ["flatpak", "info", "org.mozilla.firefox"], flatpak]
            env["FAKE_FLATPAK_STATUS"] = "1"
            assert launch(expected_status=127, available=desktop + ["snap", "flatpak"]) is None
            assert calls() == [["snap", "list", "firefox"], ["flatpak", "info", "org.mozilla.firefox"]]
            assert launch(expected_status=127, available=desktop) is None
            assert calls() == []

            # Other explicit choices must also bypass the generic portal.
            # Preserve Chrome's existing fallback order; Edge uses Custom.
            for mode, available, executable in (
                ("chrome", ["google-chrome-stable", "google-chrome", "chromium"], "google-chrome-stable"),
                ("chrome", ["google-chrome", "chromium"], "google-chrome"),
                ("chrome", ["chromium"], "chromium"),
                ("chromium", ["google-chrome-stable", "chromium"], "chromium"),
                ("microsoft-edge", ["microsoft-edge"], "microsoft-edge"),
            ):
                env["NM_GPCLIENT_BROWSER"] = mode
                assert launch(available=desktop + available) == [executable, url]
                assert calls() == [[executable, url]]

            # Generic modes retain portal -> gio -> xdg-open selection.
            for mode in ("default", "auto", ""):
                env["NM_GPCLIENT_BROWSER"] = mode
                expected = portal if has_bus and has_gdbus else ["gio", "open", url]
                assert launch(available=desktop) == expected
                assert calls() == [expected]
                assert launch(available=["xdg-open"]) == ["xdg-open", url]
            env["NM_GPCLIENT_BROWSER"] = "firefox"

    # Packaged browser commands resolve through the inherited PATH.
    env["NM_GPCLIENT_BROWSER"] = "custom-browser"
    assert launch() == ["custom-browser", url]
    env["NM_GPCLIENT_BROWSER"] = str(binaries / "custom-browser")
    assert launch() == ["custom-browser", url]

    # Diagnostics require explicit opt-in.
    env["NM_GPCLIENT_BROWSER"] = "custom-browser"
    env["NM_GPCLIENT_BROWSER_LOG"] = str(root / "debug.log")
    launch()
    assert "browser=custom-browser" in (root / "debug.log").read_text()
    env["NM_GPCLIENT_BROWSER"] = "firefox"
    assert launch(expected_status=127, available=[]) is None
    assert "Firefox selected, but no supported Firefox installation found" in (root / "debug.log").read_text()

    for browser in ("manual", "remote"):
        env["NM_GPCLIENT_BROWSER"] = browser
        assert launch(expected_status=2) is None
        assert calls() == []
    assert launch(expected_status=2, with_url=False) is None
    assert calls() == []
    assert not (root / "slept").exists()

print(f"browser-helper tests passed ({checks} simulated routing checks).")
PY
