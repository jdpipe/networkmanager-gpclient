#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

service_file=${1:-$(pkg-config --variable=vpnservicedir libnm)/nm-gpclient-service.name}
plugin=$(awk -F= '
	$0 == "[libnm]" { in_libnm = 1; next }
	/^\[/ { in_libnm = 0 }
	in_libnm && $1 == "plugin" { print $2; exit }
' "$service_file")

case "$plugin" in
	/*) ;;
	*) plugin="${NM_GPCLIENT_PLUGIN_DIR:-$(pkg-config --variable=plugindir libnm)}/$plugin" ;;
esac

if ! test -f "$plugin"; then
	echo "Installed libnm plugin is missing: $plugin" >&2
	exit 1
fi

./tools/test-editor-plugin.sh "$plugin"
