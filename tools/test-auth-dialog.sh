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

output=$(
	printf '%s\n' \
		'DATA_KEY=gateway' \
		'DATA_VAL=gateway.example.edu' \
		'DATA_KEY=as_gateway' \
		'DATA_VAL=yes' \
		'DATA_KEY=browser' \
		'DATA_VAL=default' \
		'DATA_KEY=host_id' \
		'DATA_VAL=unsupported-by-fake-gpauth' \
		'DONE' |
		./auth-dialog/nm-gpclient-auth-dialog \
			--uuid test-uuid \
			--name gpclient-test \
			--service org.freedesktop.NetworkManager.gpclient \
			--allow-interaction
)

grep -Fxq -- '--gateway gateway.example.edu --browser default' "$GPAUTH_ARGV_LOG"
printf '%s\n' "$output" | grep -Fxq 'gateway'
printf '%s\n' "$output" | grep -Fxq 'gateway.example.edu'
printf '%s\n' "$output" | grep -Fxq 'cookie'
printf '%s\n' "$output" | grep -Fq '"username":"DOMAIN\\user"'

echo "auth-dialog direct-gateway smoke test passed."
