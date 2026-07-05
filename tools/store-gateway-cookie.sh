#!/bin/sh
set -eu

name="${1:-gpclient-test}"
gateway="${2:-gateway.example.edu}"
browser="${3:-default}"

json_file="${TMPDIR:-/tmp}/nm-gpclient-gateway-auth.json"
err_file="${TMPDIR:-/tmp}/nm-gpclient-gateway-auth.err"

rm -f "$json_file" "$err_file"
gpauth --gateway "$gateway" --browser "$browser" >"$json_file" 2>"$err_file"

if ! test -s "$json_file"; then
	echo "gpauth did not produce an auth JSON result." >&2
	test ! -s "$err_file" || tail -n 80 "$err_file" >&2
	exit 1
fi

# nmcli's key/value parser treats backslash as an escape character. Preserve
# JSON domain usernames such as DOMAIN\\user before escaping literal commas.
json=$(tr -d '\n' <"$json_file" | sed 's/\\/\\\\/g; s/,/\\,/g')

nmcli connection modify "$name" \
	vpn.data "gateway=$gateway,as_gateway=yes,browser=$browser" \
	vpn.secrets "cookie=$json"

echo "Stored fresh gateway auth JSON for '$name'."
