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

## Supported Version

This plugin requires unmodified upstream `gpclient` and `gpauth` 3.x. Install
both from the same upstream version. The older 2.x callback-file protocol is
no longer supported.

External-browser authentication uses upstream's per-user Unix callback socket.
Keep upstream's `gpgui.desktop` URL handler installed: its `gpclient launch-gui`
command forwards `globalprotectcallback:` URLs to `gpauth auth-callback` without
opening the upstream GUI. The browser helper launches immediately because
`gpauth` creates its callback socket before opening the browser.

The direct-gateway mode described below is the path that currently works well
for the tested day-to-day use case. In that sense, this is a useful prototype
for a specific user's GlobalProtect setup.

Portal mode should not yet be treated as a working general solution. Although
`gpclient` provides `--auto-gateway`, this plugin has not yet found a
reliable NetworkManager workflow for portal-first activation, gateway choice,
second-stage gateway authentication, and callback handoff. Direct-gateway mode
is the validated path at this stage.

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

The service creates a persistent TUN device as root and starts `gpclient`
under NetworkManager. The child process is intentionally
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
- Matching upstream 3.x `gpclient` and `gpauth` in the usual system path,
  with upstream's `gpgui.desktop` callback handler installed
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
  --enable-absolute-paths \
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

Packagers can configure executable and installation paths directly:

```sh
./configure \
  --with-gpclient=/absolute/path/to/gpclient \
  --with-gpauth=/absolute/path/to/gpauth \
  --with-modprobe=/absolute/path/to/modprobe \
  --with-gpclient-lock-file=/run/NetworkManager/nm-gpclient.lock \
  --with-nm-plugin-dir=/absolute/path/to/libnm/plugins \
  --with-nm-vpn-service-dir=/absolute/path/to/NetworkManager/VPN
```

The equivalent configure variables are `GPCLIENT_BINARY`, `GPAUTH_BINARY`,
`MODPROBE`, `GPCLIENT_LOCK_FILE`, `NM_PLUGIN_DIR`, and `NM_VPN_SERVICE_DIR`.
Executable paths are resolved at configure time and shared by the service,
auth dialog, and editor. The service also passes the configured `gpauth` path
to gpclient for later authentication attempts. Use `--without-modprobe` when
TUN is built in or module loading is managed elsewhere.

The PID lock defaults to `/run/NetworkManager/nm-gpclient.lock`; the service
passes this path to gpclient with `--lock-file` and uses it for stale-lock
checks. Keep this separate from upstream's `/var/run/gpclient.lock` so global
disconnect commands cannot stop a connection owned by NetworkManager. Create
its parent directory beforehand if choosing a different location.

Plugin and VPN service directories default to libnm's `pkg-config` values,
with `${libdir}/NetworkManager` and `${libdir}/NetworkManager/VPN` fallbacks
when those values are empty. `--enable-absolute-paths` writes the full plugin
filename into the service metadata; the default writes its basename for
NetworkManager to resolve in its plugin directory. This option does not
change where the plugin is installed. The desktop installation scripts enable
absolute paths explicitly.

Use `make install DESTDIR=/absolute/staging/path` for packaging. The browser
helper is installed alongside the other libexec helpers by `make install`.

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
3. Select `Connect directly to gateway` for the validated workflow.
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

### Hand-created profiles

Profiles created with `nmcli` or a keyfile must mark both `cookie` and
`gateway` secrets as `NM_SETTING_SECRET_FLAG_NOT_SAVED` (`2`). Saving these
temporary authentication results can cause a later activation to reuse expired
`gpauth`/SAML data. The editor sets the flags automatically; manual profiles
must include them:

```sh
nmcli connection add type vpn con-name gpclient-direct ifname '*' \
  vpn-type org.freedesktop.NetworkManager.gpclient \
  vpn.data 'gateway=gateway.example.edu,as_gateway=yes,browser=default,cookie-flags=2,gateway-flags=2'
```

For a keyfile, put `cookie-flags=2` and `gateway-flags=2` in the `[vpn]`
section. Keep the configured gateway host there too; the `gateway` value in
`[vpn-secrets]` is a separate, temporary authentication result.

The service rejects missing or unsafe flags before accepting credentials and
checks again before launching `gpclient`. `NOT_SAVED` combined with
`AGENT_OWNED` (`3`) is also accepted. `NOT_REQUIRED`, malformed flags, and
flags that allow credentials to be saved are rejected. NetworkManager logs
the repair instructions; the desktop may still show a generic failure.

To repair an existing profile, disconnect it first, then add the flags and
remove saved authentication results without replacing other VPN settings:

```sh
nmcli connection modify gpclient-direct \
  +vpn.data 'cookie-flags=2,gateway-flags=2' \
  -vpn.secrets cookie -vpn.secrets gateway
```

Remove any previously saved `cookie` and `gateway` values from the desktop
agent's keyring too. Reconnect through a running desktop secret agent to
obtain fresh authentication. Browser SSO may complete without another login
prompt; the important point is that `gpauth` runs again.

Changing the flags alone does not establish that an existing cookie is fresh.
The service cannot determine the expiry of an arbitrary supplied auth result,
so clearing old secrets is part of the repair. The old
`tools/store-gateway-cookie.sh` helper is retired because it stored auth JSON
in profiles; it now prints migration guidance without changing anything.

## VPN Transport And Stability

The plugin defaults to **Use HTTPS-only VPN transport**, prioritising
reliability over potential UDP performance gains. The VPN remains encrypted
over TLS. This avoids the ESP data channel and the transition from ESP to
HTTPS if that channel fails.

This default addresses an observed GlobalProtect failure where a VPN stays
connected while idle but disconnects when traffic starts, such as opening an
SSH session. Logs may show `ESP receive error: Message too long`, followed by
`Failed to connect ESP tunnel; using HTTPS instead` and
`Gateway disconnected immediately after GET-tunnel request`. HTTPS-only
transport proved more stable in the affected setup; it is not a guarantee
against other causes of disconnection.

To enable DTLS/ESP for potentially better performance, uncheck **Use HTTPS-only
VPN transport** in the VPN editor, save, and reconnect. From the command line:

```sh
nmcli connection modify "<profile-name>" +vpn.data "no_dtls=no"
```

To restore HTTPS-only transport:

```sh
nmcli connection modify "<profile-name>" +vpn.data "no_dtls=yes"
```

Transport changes take effect on the next VPN connection. After upgrading,
existing profiles without a transport setting also use HTTPS-only on their
next connection. Explicit `no_dtls=no` settings continue to enable UDP. The
legacy `disable_udp` setting is honoured when `no_dtls` is absent; `no_dtls`
takes precedence when both are present.

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
./tools/test-reconnect.sh
./tools/test-editor-plugin.sh
```

`make check` includes cancellation during pending browser authentication: NM's
`QUIT` request, a closed request pipe, termination signals, and forced dialog
termination. Each case verifies that the helper releases its callback lock and
that a fresh authentication can immediately reuse it. These tests use a fake
helper and temporary locks; they do not touch a running VPN or browser session.

The auth dialog stops and reaps its own `gpauth` child when a request is cancelled,
with a forced stop after two seconds if necessary. Linux also stops that child
if the dialog is killed abruptly. This prevents abandoned authentication from
blocking later attempts with “Another external-browser authentication is already
in progress”. There is no need to delete callback lock files or clear browser
SSO cookies. Protocol callers must keep the request pipe open until the response
and then send `QUIT`, as NetworkManager's secret agent does.

`./tools/test-packaging.sh` additionally checks non-FHS executable paths,
module-loader errors, stale-lock cleanup, directory overrides and fallbacks,
both metadata path modes, and staged installation. It builds temporary copies
and does not install system files or activate a VPN.

To exercise the real upstream 3.x callback transport with synthetic SAML data,
run the following as your desktop user. It uses only a local HTTP server and
the per-user callback socket; it does not contact your VPN or open your browser.

```sh
./tools/test-upstream-auth.sh
# Or check newly built binaries before installing them:
./tools/test-upstream-auth.sh \
  ~/GlobalProtect-openconnect/target/release/gpclient \
  ~/GlobalProtect-openconnect/target/release/gpauth
```

This checks successful and failed authentication through the browser helper,
`gpclient launch-gui`, and `gpauth auth-callback`, plus disconnect isolation
using disposable processes and separate temporary PID files. A live NetworkManager
connect, disconnect, and reconnect test is still needed to verify SSO, tunnel
configuration, routes, DNS, and cleanup with your server.

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
timeout, the service sends `SIGKILL`. The service waits for child cleanup
before quitting and cancels the kill timeout when the child exits. Once
`gpclient` exits, the service destroys the persistent TUN device it created,
and NetworkManager removes the VPN routes and DNS state.

If `gpclient` exits unexpectedly, the plugin reports a VPN failure/disconnect
back to NetworkManager. The plugin does not currently implement its own
reconnect loop.

Intentional disconnects are treated as normal shutdown, even if gpclient
returns a nonzero exit status or needs to be killed. Other VPNs can remain
active: the plugin signals only its own child process group.

Some upstream gpclient packages install a global `pre-down.d/gpclient.down`
dispatcher hook that runs `gpclient disconnect` whenever any connection goes
down. With a shared PID lock, this can stop our VPN before NetworkManager
requests its disconnect (causing a failure notification), or stop it when an
unrelated VPN is disconnected. The plugin's separate PID lock keeps its
session outside that global hook's control. No profile-specific settings or
changes to the vendor dispatcher scripts are required.

After installing a change to the disconnect handling, reconnect the VPN so
it uses the new service and lock path. Check that an intentional disconnect
produces no failure notification. With another VPN active, disconnect each
VPN in turn and verify that the remaining VPN still carries its expected
traffic and DNS queries.

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

## Security Notes

- The long-running `gpclient` process currently runs as root, matching the
  privilege model of the known-working `sudo -E gpclient connect ...` command.
  Desktop authentication helpers run as the user identified by `SUDO_UID`,
  `SUDO_GID`, and `SUDO_USER`.
- The browser-auth JSON is passed to `gpclient` over stdin and marked
  `NOT_SAVED` in NetworkManager.
- Avoid enabling verbose VPN debug logs for normal use, because NetworkManager
  VPN logs can expose sensitive connection details.
- The browser helper does not write diagnostic logs by default. Set
  `NM_GPCLIENT_BROWSER_LOG` to an explicit private file path when debugging;
  these logs include authentication URLs and session environment details.

## Provenance

This project is derived from `NetworkManager-openconnect` and remains a
substantial fork of that codebase. Keep the original copyright notices and
author list for reused files. New gpclient-specific code should add its own
copyright attribution without removing the upstream provenance.

Codex with GPT 5.5 was used extensively in the writing of this code. Use it at your own risk.
