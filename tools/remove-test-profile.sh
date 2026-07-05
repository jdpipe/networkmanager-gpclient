#!/bin/sh
set -eu

name="${1:-gpclient-test}"

if nmcli -t -f NAME connection show | grep -Fxq "$name"; then
	nmcli connection delete "$name"
else
	echo "Connection '$name' does not exist."
fi
