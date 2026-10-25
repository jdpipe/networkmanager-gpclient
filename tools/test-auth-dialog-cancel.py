#!/usr/bin/env python3
"""Cancel pending authentication and prove its lock can be reused immediately."""
import fcntl
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

spec = importlib.util.spec_from_file_location("auth_test", Path(__file__).with_name("run-auth-dialog-test.py"))
auth_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(auth_test)
response = auth_test.response
build = Path(os.environ.get("GPCLIENT_TOP_BUILDDIR", Path(__file__).resolve().parent.parent))
dialog = build / "tools/nm-gpclient-auth-dialog-test"
request = b"DATA_KEY=gateway\nDATA_VAL=vpn.example.invalid\nDATA_KEY=as_gateway\nDATA_VAL=yes\nDONE\n"
args = [str(dialog), "--uuid", "cancel-test", "--name", "cancel-test",
        "--service", "org.freedesktop.NetworkManager.gpclient", "--allow-interaction"]


def wait_until(check):
    deadline = time.monotonic() + 5
    while not check():
        assert time.monotonic() < deadline, "timed out waiting for child/lock"
        time.sleep(0.02)


def gone(pid):
    try:
        # An orphan may await reaping by init, but must no longer hold any lock.
        return Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1].startswith("Z")
    except FileNotFoundError:
        return True


with tempfile.TemporaryDirectory(prefix="nm-auth-cancel-") as directory:
    root = Path(directory)
    fake = root / "gpauth"
    fake.write_text('''#!/usr/bin/env python3
import fcntl, os, signal, sys, time
from pathlib import Path
if sys.argv[1:] == ["--version"]:
    print("gpauth 3.2.0")
    sys.exit(0)
lock = open(os.environ["TEST_AUTH_LOCK"], "a")
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
if os.environ["TEST_AUTH_MODE"] == "success":
    print('{"success":{"preloginCookie":"fresh-test-cookie","username":"test"}}')
    sys.exit(0)
if os.environ["TEST_AUTH_MODE"] == "stubborn":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
Path(os.environ["TEST_AUTH_PID"]).write_text(str(os.getpid()))
# Exercise draining both pipes beyond their capacity before cancellation.
os.write(1, b"x" * 131072)
os.write(2, b"y" * 131072)
while True:
    time.sleep(1)
''')
    fake.chmod(0o755)
    env = dict(os.environ, PATH=str(root) + ":" + os.environ["PATH"],
               TEST_AUTH_LOCK=str(root / "callback.lock"), TEST_AUTH_PID=str(root / "pid"))
    # An unrelated process in our session must remain untouched.
    unrelated = subprocess.Popen(["python3", "-c", "import time; time.sleep(60)"])
    try:
        for method in ("QUIT", "EOF", "SIGTERM", "SIGINT", "SIGHUP", "SIGKILL", "stubborn"):
            env["TEST_AUTH_MODE"] = "stubborn" if method == "stubborn" else "wait"
            (root / "pid").unlink(missing_ok=True)
            child_pid = None
            with subprocess.Popen(args, env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                try:
                    process.stdin.write(request)
                    process.stdin.flush()
                    wait_until(lambda: (root / "pid").exists() and (root / "pid").stat().st_size)
                    child_pid = int((root / "pid").read_text())
                    if method in ("QUIT", "stubborn"):
                        # QUIT need not arrive in a single read.
                        process.stdin.write(b"QU")
                        process.stdin.flush()
                        time.sleep(0.05)
                        process.stdin.write(b"IT\n")
                        process.stdin.flush()
                    elif method == "EOF":
                        process.stdin.close()
                        process.stdin = None
                    else:
                        process.send_signal(getattr(signal, method))
                    # Keep stdin open until QUIT or the signal has been handled;
                    # communicate() would close it and mask a broken QUIT path.
                    assert process.wait(timeout=5) != 0
                    output, error = process.communicate(timeout=2)
                    assert output == b"", "cancelled authentication leaked a secret response"
                    wait_until(lambda: gone(child_pid))
                    with open(root / "callback.lock", "a") as lock:
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    assert unrelated.poll() is None
                finally:
                    if process.poll() is None:
                        process.kill()
                    if child_pid and not gone(child_pid):
                        os.kill(child_pid, signal.SIGKILL)

            # Reconnect uses the very same lock, without deleting it.
            env["TEST_AUTH_MODE"] = "success"
            with subprocess.Popen(args, env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                try:
                    process.stdin.write(request)
                    process.stdin.flush()
                    output = response(process)
                    assert b"fresh-test-cookie" in output, output
                    process.stdin.write(b"QUIT\n")
                    process.stdin.flush()
                    assert process.wait(timeout=5) == 0
                finally:
                    if process.poll() is None:
                        process.kill()
            print(f"PASS: {method} releases callback lock; fresh authentication succeeds")
    finally:
        unrelated.terminate()
        unrelated.wait(timeout=5)
