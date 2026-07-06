# NetworkManager-gpclient

`NetworkManager-gpclient` is a prototype NetworkManager VPN plugin for
GlobalProtect VPNs that are handled by
[`gpclient`](https://github.com/yuezk/GlobalProtect-openconnect).

It adds a `GlobalProtect (gpclient)` VPN type to NetworkManager-compatible
editors and lets NetworkManager start, stop, and monitor a `gpclient` VPN
session. The intent is to make day-to-day use feel like a normal
NetworkManager VPN connection, while still relying on `gpclient` for
GlobalProtect protocol handling and browser-based authentication.

This project is based on `NetworkManager-openconnect`, but it has been changed
to make use of `gpclient` rather than talking to OpenConnect directly. It
reuses substantial NetworkManager VPN plugin code, especially the service
skeleton, D-Bus integration, persistent TUN handling, packaging layout, and the
helper that translates vpnc-script style environment variables into
NetworkManager IP, DNS, and route configuration.

## Tested Version

This prototype has been tested with:

- `gpclient 2.5.4 (2026-05-09)`
- `gpclient 2.6.4 (494e4a533 2026-06-25)`

The direct-gateway mode described below is the path that currently works well
for the tested day-to-day use case. In that sense, this is a useful prototype
for a specific user's GlobalProtect setup.

Portal mode should not yet be treated as a working general solution. Although
`gpclient` 2.6.4 provides `--auto-gateway`, this plugin has not yet found a
reliable NetworkManager workflow for portal-first activation, gateway choice,
second-stage gateway authentication, and callback handoff. Direct-gateway mode
is the validated path at this stage.

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

The service creates a persistent TUN device as root and starts `gpclient`
under NetworkManager. With `gpclient` 2.6.x the child process is intentionally
kept in the same privilege shape as `sudo -E gpclient`: the main process runs
as root for tunnel/session management, while `SUDO_UID`, `SUDO_GID`,
`SUDO_USER`, and desktop environment variables identify the user session for
browser authentication relaunches. NetworkManager still owns the VPN profile,
activation state, routes, and DNS state.

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

Portal mode is present in the UI for experimentation, but it is not currently
recommended for daily use. Direct-gateway mode is the practical path for the
setup this prototype was built around.

## Gateway Selection

GlobalProtect gateway selection is not normally handled inside the browser
login page. The browser/SAML step authenticates the user; after that,
`gpclient` asks the portal for its configuration. That portal configuration can
contain one or more gateways, each with a display name, server address, default
priority, and optional region-specific priority rules.

`gpclient` uses the region from the portal prelogin response to sort the
portal-provided gateway list. With `--auto-gateway`, it tries gateways in that
priority order. With `--gateway <name-or-address>`, it connects to the named
gateway. If neither option is supplied and the portal returns multiple
gateways, the standalone `gpclient` CLI can prompt interactively for a gateway
choice.

This NetworkManager plugin cannot expose that terminal prompt safely, because
the service runs non-interactively under NetworkManager. It also does not yet
have a separate portal-discovery workflow that can authenticate, fetch the
available gateways, and present them to the user as a normal GUI choice. The
current UI therefore supports these limited modes:

- `Automatic gateway`: portal mode with no fixed gateway, using
  `gpclient --auto-gateway`.
- `Specific gateway`: portal mode with a configured gateway name or address,
  passed to `gpclient --gateway`.
- `Connect directly to gateway`: skip portal gateway discovery and treat the
  configured gateway as the GlobalProtect gateway.

This means the portal path does not currently provide the full GlobalProtect
portal experience. In particular, users who are expected to choose a
geographically local or otherwise policy-preferred gateway from the portal's
gateway list cannot do that through this plugin yet. A richer future UI could
authenticate to the portal, fetch the gateway list, and populate a gateway
selector in the editor. That would require an explicit discovery workflow;
NetworkManager editor plugins are otherwise mostly static profile editors, not
post-login selection wizards.

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

- This is prototype software, validated for one working direct-gateway setup.
- Portal-first mode is not solved. It may authenticate in the browser, but it
  has not been made reliable through gateway selection, second gateway auth,
  and callback handoff.
- There is no gateway picker for portal-provided gateways; users must use
  automatic selection, pre-enter a known gateway, or connect directly to a
  known gateway.
- The raw TUN device may also appear in NetworkManager as an assumed `vpn0`
  device, even when the VPN profile itself is active.
- Client-certificate support is not polished. In particular, key passwords
  should not be considered production-ready because they can be passed to
  `gpclient` as command-line arguments.
- The stock `gpclient` package may install its own NetworkManager dispatcher
  hooks. Those hooks are outside this plugin and may need review if network
  transition behavior looks surprising.

## Security Notes

- The long-running `gpclient` process currently runs as root, matching the
  privilege model of the known-working `sudo -E gpclient connect ...` command.
  This is a deliberate `gpclient` 2.6.x compatibility tradeoff, not an ideal
  final security boundary.
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
