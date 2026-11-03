#!/bin/sh
# Inspect a built package without installing it or changing NetworkManager.
set -eu

package=$(realpath "${1:?usage: tools/test-deb.sh package.deb}")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
dpkg-deb --extract "$package" "$work/root"
dpkg-deb --control "$package" "$work/control"

test "$(dpkg-deb --field "$package" Package)" = network-manager-gpclient
arch=$(dpkg-deb --field "$package" Architecture)
multiarch=$(dpkg-architecture -a"$arch" -qDEB_HOST_MULTIARCH)
plugins=/usr/lib/$multiarch/NetworkManager
for helper in nm-gpclient-service nm-gpclient-service-helper nm-gpclient-auth-dialog nm-gpclient-browser-helper; do
	test -x "$work/root/usr/libexec/$helper"
done
for plugin in libnm-vpn-plugin-gpclient.so libnm-vpn-plugin-gpclient-editor.so libnm-gtk4-vpn-plugin-gpclient-editor.so; do
	test -f "$work/root$plugins/$plugin"
done
metadata=$work/root/usr/lib/NetworkManager/VPN/nm-gpclient-service.name
grep -Fxq "plugin=$plugins/libnm-vpn-plugin-gpclient.so" "$metadata"
grep -Fxq 'program=/usr/libexec/nm-gpclient-service' "$metadata"
test -f "$work/root/usr/share/dbus-1/system.d/nm-gpclient-service.conf"
test -f "$work/root/usr/lib/sysusers.d/nm-gpclient-sysusers.conf"
grep -Fxq 'managed=0' "$work/root/usr/lib/NetworkManager/conf.d/90-nm-gpclient-vpn0-unmanaged.conf"
grep -Fq systemd-sysusers "$work/control/postinst"
grep -Fq org.freedesktop.DBus.ReloadConfig "$work/control/postinst"
dpkg-deb --field "$package" Depends | grep -Fq 'globalprotect-openconnect (>= 3.2.0)'
dpkg-deb --field "$package" Depends | grep -Fq 'globalprotect-openconnect (<< 4.0~)'
test -z "$(find "$work/root" -name '*.la' -o -name '*.a')"
test ! -e "$work/root/usr/bin/gpclient"
test ! -e "$work/root/usr/bin/gpauth"

# Load the actual packaged libnm plugin and validate its advertised service.
"$(dirname "$0")/test-editor-plugin.sh" "$work/root$plugins/libnm-vpn-plugin-gpclient.so"
echo "PASS: Debian package contents, dependencies, maintainer scripts, and libnm plugin"
