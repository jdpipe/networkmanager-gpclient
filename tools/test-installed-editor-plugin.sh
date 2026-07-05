#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

plugin=$(awk -F= '
	$0 == "[libnm]" { in_libnm = 1; next }
	/^\[/ { in_libnm = 0 }
	in_libnm && $1 == "plugin" { print $2; exit }
' /usr/lib/NetworkManager/VPN/nm-gpclient-service.name)

if ! test -f "$plugin"; then
	echo "Installed libnm plugin is missing: $plugin" >&2
	exit 1
fi

./tools/test-editor-plugin.sh "$plugin"
