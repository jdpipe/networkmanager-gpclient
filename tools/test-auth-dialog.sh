#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

# The installed auth dialog uses a fixed configured path; this test fixture
# deliberately resolves a fake gpauth from the test PATH.
make -s tools/nm-gpclient-auth-dialog-test

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cat >"$tmpdir/gpauth" <<'EOF'
#!/bin/sh
if [ "${1:-}" = "--version" ]; then
	printf 'gpauth %s\n' "${FAKE_GPAUTH_VERSION:-3.2.0}"
	exit 0
fi
if [ "${1:-}" = "--help" ]; then
	cat <<'HELP'
Usage: gpauth [OPTIONS] <SERVER>
      --gateway
      --browser [<BROWSER>]
      --os <OS>
      --fix-openssl
      --ignore-tls-errors
HELP
	exit 0
fi
printf '%s\n' "$*" >"$GPAUTH_ARGV_LOG"
printf '%s\n' '{"success":{"portalUserauthcookie":"","preloginCookie":"test-cookie","token":null,"username":"DOMAIN\\user"}}'
EOF
chmod +x "$tmpdir/gpauth"

export PATH="$tmpdir:$PATH"
export GPAUTH_ARGV_LOG="$tmpdir/gpauth.argv"

run_dialog() {
	printf '%s\n' \
		'DATA_KEY=gateway' \
		'DATA_VAL=gateway.example.edu' \
		'DATA_KEY=as_gateway' \
		'DATA_VAL=yes' \
		'DATA_KEY=browser' \
		'DATA_VAL=default' \
		'DATA_KEY=host_id' \
		'DATA_VAL=test-runtime-host-id' \
		'DONE' |
		python3 ./tools/run-auth-dialog-test.py ./tools/nm-gpclient-auth-dialog-test \
			--uuid test-uuid \
			--name gpclient-test \
			--service org.freedesktop.NetworkManager.gpclient \
			--allow-interaction
}

output=$(run_dialog)

grep -Fxq -- '--gateway gateway.example.edu --browser default --host-id test-runtime-host-id' "$GPAUTH_ARGV_LOG"
printf '%s\n' "$output" | grep -Fxq 'gateway'
printf '%s\n' "$output" | grep -Fxq 'gateway.example.edu'
printf '%s\n' "$output" | grep -Fxq 'cookie'
printf '%s\n' "$output" | grep -Fq '"username":"DOMAIN\\user"'

for version in 2.6.5 4.0.0 invalid; do
	export FAKE_GPAUTH_VERSION=$version
	rm -f "$GPAUTH_ARGV_LOG"
	if run_dialog >"$tmpdir/rejected.output" 2>"$tmpdir/rejected.error"; then
		echo "auth dialog accepted unsupported gpauth $version" >&2
		exit 1
	fi
	test ! -e "$GPAUTH_ARGV_LOG"
	test ! -s "$tmpdir/rejected.output"
	grep -Fq 'gpauth 3.x is required' "$tmpdir/rejected.error"
done

echo "auth-dialog direct-gateway and version prerequisite tests passed."
