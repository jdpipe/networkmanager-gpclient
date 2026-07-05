#!/bin/sh
set -eu

portal="${1:-vpn.example.edu}"
name="${2:-gpclient-test}"
browser="${3:-default}"

if nmcli -t -f NAME connection show | grep -Fxq "$name"; then
	echo "Connection '$name' already exists."
	echo "Delete it first with: nmcli connection delete '$name'"
	exit 1
fi

nmcli connection add \
	type vpn \
	con-name "$name" \
	ifname "*" \
	vpn-type org.freedesktop.NetworkManager.gpclient \
	vpn.data "portal=$portal,auto_gateway=yes,browser=$browser"

echo "Created '$name' for portal '$portal'."
echo "Start it with: nmcli connection up '$name'"
