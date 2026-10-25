#!/bin/sh
set -eu

srcdir=${GPCLIENT_TOP_SRCDIR:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}
builddir=${GPCLIENT_TOP_BUILDDIR:-$srcdir}
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

# Capture the real profile creation command without touching the system NM.
cat >"$tmpdir/nmcli" <<'EOF'
#!/bin/sh
if [ "$1" = '-t' ]; then
	exit 0
fi
while [ "$#" -gt 0 ]; do
	if [ "$1" = 'vpn.data' ]; then
		printf '%s\n' "$2" >"$PROFILE_DATA"
		exit 0
	fi
	shift
done
exit 1
EOF

cat >"$tmpdir/gpauth" <<'EOF'
#!/bin/sh
if [ "${1:-}" = '--version' ]; then
	printf '%s\n' 'gpauth 3.2.0'
	exit 0
fi
if [ "${1:-}" = '--help' ]; then
	printf '%s\n' '--gateway --browser'
	exit 0
fi
printf '%s\n' "$*" >>"$GPAUTH_CALLS"
printf '{"success":{"preloginCookie":"%s","username":"test-user"}}\n' "$GPAUTH_COOKIE"
EOF
chmod +x "$tmpdir/nmcli" "$tmpdir/gpauth"
export PATH="$tmpdir:$PATH"
export PROFILE_DATA="$tmpdir/profile.data" GPAUTH_CALLS="$tmpdir/gpauth.calls"

"$srcdir/tools/create-test-profile.sh" vpn.example.edu reconnect-test default
"$builddir/tools/test-reconnect" "$(cat "$PROFILE_DATA")" \
	"$builddir/auth-dialog/nm-gpclient-auth-dialog"

# Both modelled activations in each mode must run gpauth.
test "$(wc -l <"$GPAUTH_CALLS")" -eq 4
test "$(grep -Fxc -- 'vpn.example.edu --browser default' "$GPAUTH_CALLS")" -eq 2
test "$(grep -Fxc -- '--gateway gateway.example.edu --browser default' "$GPAUTH_CALLS")" -eq 2
echo 'reconnect secret lifecycle regression passed.'
