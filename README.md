# NetworkManager-gpclient

`NetworkManager-gpclient` is a prototype NetworkManager VPN plugin for
GlobalProtect VPNs that are handled by
[GlobalProtect-openconnect](https://github.com/yuezk/GlobalProtect-openconnect) aka `gpclient`.

It adds a `GlobalProtect (gpclient)` VPN type to NetworkManager-compatible
editors and lets NetworkManager start, stop, and monitor a `gpclient` VPN
session. The intent is to make day-to-day use feel like a normal
NetworkManager VPN connection, while still relying on `gpclient` (via the standard globalprotect-openconnect package) for
GlobalProtect protocol handling and browser-based authentication.

This project is based on [NetworkManager-openconnect](https://github.com/GNOME/NetworkManager-openconnect), but with changes
to make use of `gpclient` rather than talking to OpenConnect directly. It
reuses substantial NetworkManager VPN plugin code, especially the service
skeleton, D-Bus integration, persistent TUN handling, packaging layout, and the
helper that translates vpnc-script style environment variables into
NetworkManager IP, DNS, and route configuration.

Another project which appears to provide similar functionality is [GlobalProtect-SAML-NetworkManager](https://github.com/WMP/GlobalProtect-SAML-NetworkManager). This project differs in that it uses the standard globalprotect-openconnect package (gpclient, etc.) rather than bundling its own copy. Also, it doesn't attempt to offer KDE integration at this stage.

## Tested Version

This prototype has been tested with:

- `gpclient 2.5.4 (2026-05-09)`

The direct-gateway mode described below is the path tested most thoroughly.
Portal mode is less reliable with `gpclient` 2.5.x because that release does
not provide the newer `--auto-gateway` behavior.

| ![](https://github.com/user-attachments/assets/770c4053-c673-496e-a789-3321f14c4f00) | ![](https://github.com/user-attachments/assets/72f712ab-d54c-44da-b18b-e91373d36d74) |
|-----------|--------|

## How It Works

The plugin has three main runtime pieces:

- `nm-gpclient-service`: the NetworkManager VPN service plugin. NetworkManager
  starts this as root when the VPN profile is activated.
- `nm-gpclient-auth-dialog`: the NetworkManager auth helper. It runs in the
  user's session, invokes `gpauth`, and lets `gpclient` open the configured
  browser for SAML/browser authentication.
- `nm-gpclient-service-helper`: the script hook passed to `gpclient` with
  `--script`. It receives the VPN address, DNS, and route information from
  `gpclient`/OpenConnect and reports that configuration back to NetworkManager.

On activation, NetworkManager asks the auth helper for secrets. The auth helper
uses browser-based `gpauth` and returns the resulting gpclient auth JSON as a
temporary NetworkManager secret. The service then starts `gpclient connect`
with `--cookie-on-stdin`, passes the auth JSON over stdin, and points
`gpclient` at the NetworkManager helper script.

The service creates a persistent TUN device as root, assigns it to the
dedicated `nm-gpclient` user, and then drops the `gpclient` child process to
that user. This keeps the long-running VPN process out of root while allowing
NetworkManager to own the interface, routes, and DNS state.

## How This Differs From NetworkManager-openconnect

`NetworkManager-openconnect` is built around OpenConnect as the VPN engine.
Its auth dialog obtains OpenConnect-oriented secrets, and its service starts
the `openconnect` binary directly with those secrets. NetworkManager is the
primary orchestration layer, while OpenConnect is mostly the transport engine.

`NetworkManager-gpclient` is deliberately different. `gpclient` already
contains GlobalProtect-specific behavior that is not just transport plumbing:
browser/SAML authentication, GlobalProtect portal and gateway handling,
client-version and reported-OS behavior, HIP integration, and other protocol
quirks. This plugin therefore treats `gpclient` as the GlobalProtect authority
and wraps it in NetworkManager lifecycle management.

In practice, that means:

- the auth helper invokes `gpauth` and lets `gpclient` handle browser-based
  authentication instead of rendering or parsing login forms itself;
- the service starts `gpclient connect`, not `openconnect`;
- the NetworkManager helper still receives vpnc-script style environment from
  the underlying OpenConnect layer and reports IP configuration back to
  NetworkManager;
- NetworkManager owns the profile, activation state, routes, DNS, and
  disconnect request, while `gpclient` owns GlobalProtect-specific protocol
  decisions.

So this is not a thin rename of the OpenConnect plugin. It is a fork that keeps
the NetworkManager plugin scaffolding and configuration handoff model, but
moves the GlobalProtect-specific connection logic under `gpclient`.

## Dependencies

Runtime:

- NetworkManager with libnm VPN plugin support
- `gpclient` and `gpauth` in the usual system path
- A desktop/browser environment if browser authentication is required

Build-time core dependencies on Debian/Ubuntu:

```sh
sudo apt install build-essential autoconf automake libtool pkg-config \
  libglib2.0-dev libxml2-dev libnm-dev
```

For the GNOME Settings / NetworkManager editor integration:

```sh
sudo apt install libgtk-3-dev libgtk-4-dev libnma-dev
```

Package names differ across distributions. Fedora-style equivalents include
`NetworkManager-libnm-devel`, `gtk3-devel`, `gtk4-devel`, and `libnma-devel`.

## Build And Install

For normal desktop use, build and install the core service plus the editor
plugins:

```sh
cd ~/networkmanager-gpclient
./tools/install-gui.sh
```

That script configures the project with:

```sh
./configure --prefix=/usr \
  --libexecdir=/usr/libexec \
  --sysconfdir=/etc \
  --localstatedir=/var \
  --with-gnome=yes \
  --with-gtk4=yes \
  --with-authdlg=yes
```

Then it runs `make`, installs with `sudo make install`, creates the
`nm-gpclient` system user where supported, reloads D-Bus policy, and verifies
that the libnm editor plugins landed in NetworkManager's plugin directory.

For a service-only install without the editor plugins:

```sh
cd ~/networkmanager-gpclient
./tools/install-core.sh
```

If `GlobalProtect (gpclient)` does not appear immediately in the Add VPN
workflow, close and reopen the editor. If NetworkManager still does not see the
new service, disconnect active VPNs and restart NetworkManager:

```sh
sudo systemctl restart NetworkManager
```

## Creating A VPN Profile

In GNOME Settings or another compatible NetworkManager editor:

1. Open the Add VPN workflow.
2. Select `GlobalProtect (gpclient)`.
3. For `gpclient` 2.5.x, use `Connect directly to gateway`.
4. Enter the gateway host in `Gateway`.
5. Leave `Browser` as `Default` unless a specific browser command is needed.
6. Save the profile and activate it from NetworkManager.

This corresponds roughly to:

```sh
gpclient connect gateway.example.edu --browser default --as-gateway
```

but NetworkManager performs the privileged setup, starts the auth helper in the
user session, and owns the resulting routes and DNS.

Portal mode is present in the UI, but with `gpclient` 2.5.x direct-gateway mode
is the practical path. Newer `gpclient` versions with `--auto-gateway` should
be a better fit for portal-first activation, but that path needs more testing.

## Validation

After building, these smoke tests should pass:

```sh
make check
./tools/test-auth-dialog.sh
./tools/test-auth-dialog-quit.sh
./tools/test-editor-plugin.sh
```

To verify the installed editor plugin:

```sh
./tools/test-installed-editor-plugin.sh
```

To inspect runtime logs after an activation attempt:

```sh
journalctl -b --no-pager --since '10 minutes ago' | \
  rg 'nm-gpclient|gpclient|NetworkManager|vpn'
```

## Disconnect Behavior

When the VPN is disconnected through NetworkManager, the service sends
`SIGINT` to the running `gpclient` process. If it has not exited after a short
timeout, the service sends `SIGKILL`. Once `gpclient` exits, the service
destroys the persistent TUN device it created, and NetworkManager removes the
VPN routes and DNS state.

If `gpclient` exits unexpectedly, the plugin reports a VPN failure/disconnect
back to NetworkManager. The plugin does not currently implement its own
reconnect loop.

## Current Limitations

- This is prototype software, tested on one working direct-gateway setup.
- `gpclient` 2.5.x portal mode is not yet a reliable day-to-day path.
- The raw TUN device may also appear in NetworkManager as an assumed `vpn0`
  device, even when the VPN profile itself is active.
- Client-certificate support is not polished. In particular, key passwords
  should not be considered production-ready because they can be passed to
  `gpclient` as command-line arguments.
- The stock `gpclient` package may install its own NetworkManager dispatcher
  hooks. Those hooks are outside this plugin and may need review if network
  transition behavior looks surprising.

## Security Notes

- The long-running `gpclient` process is dropped to the dedicated
  `nm-gpclient` user after the service creates the persistent TUN device.
- The browser-auth JSON is passed to `gpclient` over stdin and marked
  `NOT_SAVED` in NetworkManager.
- Avoid enabling verbose VPN debug logs for normal use, because NetworkManager
  VPN logs can expose sensitive connection details.

## Provenance

This project is derived from `NetworkManager-openconnect` and remains a
substantial fork of that codebase. Keep the original copyright notices and
author list for reused files. New gpclient-specific code should add its own
copyright attribution without removing the upstream provenance.

Codex with GPT 5.5 was used extensively in the writing of this code. Use it at your own risk.
