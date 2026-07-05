#!/bin/sh
set -eu

portal="${1:-vpn.example.edu}"
name="${2:-gpclient-test}"
browser="${3:-default}"

if pgrep -x gpclient >/dev/null 2>&1; then
	echo "A gpclient process is already running."
	echo "Disconnect the current gpclient VPN before running this test."
	exit 1
fi

if ! test -f /usr/lib/NetworkManager/VPN/nm-gpclient-service.name; then
	echo "NetworkManager-gpclient is not installed in /usr/lib/NetworkManager/VPN."
	echo "Run: ./tools/install-core.sh"
	exit 1
fi

if ! nmcli -t -f NAME connection show | grep -Fxq "$name"; then
	"$(dirname "$0")/create-test-profile.sh" "$portal" "$name" "$browser"
fi

echo "Starting '$name'. Browser auth should open via gpauth if interaction is needed."
nmcli connection up "$name"
