#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

./configure --prefix=/usr --libexecdir=/usr/libexec --sysconfdir=/etc --localstatedir=/var
make -j"$(nproc)"
sudo make install
sudo rm -f /usr/lib/NetworkManager/conf.d/nm-gpclient-unmanaged.conf

if command -v systemd-sysusers >/dev/null 2>&1; then
	sudo systemd-sysusers /usr/lib/sysusers.d/nm-gpclient-sysusers.conf || true
fi

if systemctl --quiet is-active dbus.service 2>/dev/null; then
	sudo systemctl reload dbus.service || true
fi

if systemctl --quiet is-active dbus-broker.service 2>/dev/null; then
	sudo systemctl reload dbus-broker.service || true
fi

echo "Installed NetworkManager-gpclient core files."
echo "If NetworkManager does not notice the VPN service immediately, restart NetworkManager after disconnecting any active VPN."
