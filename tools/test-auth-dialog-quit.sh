#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cat >"$tmpdir/gpauth" <<'EOF'
#!/bin/sh
if [ "${1:-}" = "--help" ]; then
	cat <<'HELP'
Usage: gpauth [OPTIONS] <SERVER>
      --browser [<BROWSER>]
HELP
	exit 0
fi
printf '%s\n' '{"success":{"portalUserauthcookie":"","preloginCookie":"test-cookie","token":null,"username":"DOMAIN\\user"}}'
EOF
chmod +x "$tmpdir/gpauth"

export PATH="$tmpdir:$PATH"

mkfifo "$tmpdir/stdin" "$tmpdir/stdout"

./auth-dialog/nm-gpclient-auth-dialog \
	--uuid test-uuid \
	--name gpclient-test \
	--service org.freedesktop.NetworkManager.gpclient \
	--allow-interaction \
	<"$tmpdir/stdin" >"$tmpdir/stdout" 2>"$tmpdir/stderr" &
pid=$!

(
	sleep 15
	kill "$pid" 2>/dev/null || true
) &
watchdog=$!

exec 3>"$tmpdir/stdin"
printf '%s\n' \
	'DATA_KEY=portal' \
	'DATA_VAL=vpn.example.edu' \
	'DATA_KEY=auto_gateway' \
	'DATA_VAL=yes' \
	'DATA_KEY=browser' \
	'DATA_VAL=default' \
	'DONE' >&3

blank_count=0
: >"$tmpdir/output"
while IFS= read -r line; do
	printf '%s\n' "$line" >>"$tmpdir/output"
	if [ -z "$line" ]; then
		blank_count=$((blank_count + 1))
	else
		blank_count=0
	fi
	if [ "$blank_count" -ge 2 ]; then
		break
	fi
done <"$tmpdir/stdout"

if ! kill -0 "$pid" 2>/dev/null; then
	echo "auth dialog exited before QUIT" >&2
	cat "$tmpdir/stderr" >&2
	exit 1
fi

printf '%s\n\n' 'QUIT' >&3
exec 3>&-

wait "$pid"
kill "$watchdog" 2>/dev/null || true

grep -Fxq 'gateway' "$tmpdir/output"
grep -Fxq 'vpn.example.edu' "$tmpdir/output"
grep -Fxq 'cookie' "$tmpdir/output"
grep -Fq '"username":"DOMAIN\\user"' "$tmpdir/output"

echo "auth-dialog QUIT handshake smoke test passed."
