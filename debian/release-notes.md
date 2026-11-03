This release provides one `network-manager-gpclient` package containing the
NetworkManager VPN service, authentication helpers, and GTK 3/GTK 4 editor plugins.

Choose the `.deb` matching your Ubuntu release (24.04 or 26.04) and architecture
(`dpkg --print-architecture`). The Ubuntu version appears in the package filename.

Install upstream's `globalprotect-openconnect` 3.x dependency first. On Ubuntu:

```sh
sudo apt install software-properties-common
sudo add-apt-repository ppa:yuezk/globalprotect-openconnect
sudo apt update
sudo apt install globalprotect-openconnect
```

Alternatively, obtain a compatible 3.x `.deb` from the
[upstream releases](https://github.com/yuezk/GlobalProtect-openconnect/releases)
and install it with `sudo apt install ./globalprotect-openconnect_*.deb`.
Version 3.2.0 or newer within the 3.x series is required. Keep the upstream
`gpgui.desktop` browser callback handler installed.

Then install the downloaded plugin package:

```sh
sudo apt install ./network-manager-gpclient_*.deb
```

Run that command in a directory containing only the plugin package for your
Ubuntu release and architecture. Reopen your network settings or VPN editor
after installation. If the VPN type still does not appear, disconnect active
VPNs before restarting NetworkManager.

Direct gateway connections are the currently validated mode. See the
[README](https://github.com/jdpipe/networkmanager-gpclient#readme) for setup and
the current portal-mode limitations.

`SHA256SUMS` covers the binary packages and the corresponding source archive.
