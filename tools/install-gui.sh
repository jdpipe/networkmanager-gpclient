#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

./configure --prefix=/usr --libexecdir=/usr/libexec --sysconfdir=/etc --localstatedir=/var --with-gnome=yes --with-gtk4=yes --with-authdlg=yes
make -j"$(nproc)"
sudo make install
sudo install -m 755 src/nm-gpclient-browser-helper /usr/libexec/nm-gpclient-browser-helper
sudo mkdir -p /etc/NetworkManager/conf.d
printf "[device-nm-gpclient-vpn0]\nmatch-device=interface-name:vpn0\nmanaged=0\n" | sudo tee /etc/NetworkManager/conf.d/90-nm-gpclient-vpn0-unmanaged.conf >/dev/null
sudo rm -f /usr/lib/NetworkManager/conf.d/nm-gpclient-unmanaged.conf
sudo chown root:root /usr/lib/NetworkManager/dispatcher.d/gpclient-nm-hook /usr/lib/NetworkManager/dispatcher.d/pre-down.d/gpclient.down 2>/dev/null || true

sudo rm -f /usr/lib/NetworkManager/libnm-vpn-plugin-gpclient-editor.so \
	/usr/lib/NetworkManager/libnm-vpn-plugin-gpclient-editor.la \
	/usr/lib/NetworkManager/libnm-vpn-plugin-gpclient-editor.a \
	/usr/lib/NetworkManager/libnm-vpn-plugin-gpclient.so \
	/usr/lib/NetworkManager/libnm-vpn-plugin-gpclient.la \
	/usr/lib/NetworkManager/libnm-vpn-plugin-gpclient.a \
	/usr/lib/NetworkManager/libnm-gtk4-vpn-plugin-gpclient-editor.so \
	/usr/lib/NetworkManager/libnm-gtk4-vpn-plugin-gpclient-editor.la \
	/usr/lib/NetworkManager/libnm-gtk4-vpn-plugin-gpclient-editor.a

plugin_path=$(pkg-config --variable=plugindir libnm)/libnm-vpn-plugin-gpclient.so
if ! test -f "$plugin_path"; then
	echo "Expected libnm plugin was not installed: $plugin_path" >&2
	exit 1
fi
for editor in libnm-vpn-plugin-gpclient-editor.so libnm-gtk4-vpn-plugin-gpclient-editor.so; do
	if ! test -f "$(pkg-config --variable=plugindir libnm)/$editor"; then
		echo "Expected editor plugin was not installed: $(pkg-config --variable=plugindir libnm)/$editor" >&2
		exit 1
	fi
done

if ! grep -Fq "$plugin_path" /usr/lib/NetworkManager/VPN/nm-gpclient-service.name; then
	echo "VPN service metadata does not point at $plugin_path" >&2
	exit 1
fi

if command -v systemd-sysusers >/dev/null 2>&1; then
	sudo systemd-sysusers /usr/lib/sysusers.d/nm-gpclient-sysusers.conf || true
fi

if systemctl --quiet is-active dbus.service 2>/dev/null; then
	sudo systemctl reload dbus.service || true
fi

if systemctl --quiet is-active dbus-broker.service 2>/dev/null; then
	sudo systemctl reload dbus-broker.service || true
fi

echo "Installed NetworkManager-gpclient core and GNOME editor files."
echo "Restart NetworkManager or reopen the NetworkManager editor if the new VPN type is not shown immediately."
