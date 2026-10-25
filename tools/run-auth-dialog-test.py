#!/usr/bin/env python3
"""Send test input to an auth dialog, keeping NM's pipe open until its reply."""
import os
import select
import subprocess
import sys
import time


def response(process, timeout=10):
    output = b""
    deadline = time.monotonic() + timeout
    while not output.endswith(b"\n\n\n"):
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([process.stdout], [], [], remaining)[0]:
            raise TimeoutError("auth dialog did not respond")
        chunk = os.read(process.stdout.fileno(), 4096)
        if not chunk:
            break
        output += chunk
    return output


if __name__ == "__main__":
    with subprocess.Popen(sys.argv[1:], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
        try:
            process.stdin.write(sys.stdin.buffer.read())
            process.stdin.flush()
            output = response(process)
            if output:
                process.stdin.write(b"QUIT\n")
                process.stdin.flush()
            process.stdin.close()
            result = process.wait(timeout=5)
            sys.stdout.buffer.write(output)
        finally:
            if process.poll() is None:
                process.kill()
        sys.exit(result)
