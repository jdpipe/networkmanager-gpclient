# NetworkManager-gpclient prototype

This tree is a prototype fork of `NetworkManager-openconnect` for running
`gpclient` under NetworkManager.

## Current split

The current checkpoint has two pieces:

1. NetworkManager activates a VPN profile with service type
   `org.freedesktop.NetworkManager.gpclient`.
2. `nm-gpclient-service` receives the VPN settings.
3. In the normal browser-auth path, it starts
   `gpclient connect <portal> --browser <browser>` and lets gpclient perform its
   own portal/gateway discovery and fallback authentication.
4. If a `cookie` secret is explicitly supplied, it uses the diagnostic
   `gpclient --cookie-on-stdin` path instead.
5. It passes `--script @LIBEXECDIR@/nm-gpclient-service-helper`.
6. The helper reuses NetworkManager-openconnect's vpnc-script environment parser
   and reports IP/DNS/routes back to NetworkManager.

There is now a minimal GNOME/libnm editor plugin. It advertises
`GlobalProtect (gpclient)` in NetworkManager editors and exposes the core
settings needed for the current prototype: portal, gateway, direct-gateway
mode, browser, reported OS, client version, key password, and basic tunnel
flags. The plugin uses libnma for standard secret-storage handling on the key
password field.

## Supported service settings

The service currently reads these VPN data items:

- `portal`: optional portal server. If set, the service runs
  `gpclient connect <portal>`.
- `auto_gateway=yes`: with `portal`, do not pass a fixed `--gateway`; let
  `gpclient` use its native portal/gateway handling. This is compatible with
  gpclient 2.5.x, which does not have a `--auto-gateway` CLI flag.
- `as_gateway=yes`: without `portal`, pass `--as-gateway`.
- `browser`: browser mode for gpclient, for example `default`, `auto`, or
  `remote`. Defaults to `default`.
- `mtu`: pass `--mtu`.
- `usercert`: pass `--certificate`.
- `userkey`: pass `--sslkey`.
- `client_version`: pass `--client-version` to gpclient.
- `host_id`: pass `--host-id` to `gpauth` when using the diagnostic
  cookie-on-stdin flow.
- `reported_os`: pass `--os` to gpclient.
- `fix_openssl=yes`: pass `--fix-openssl` to gpclient.
- `ignore_tls_errors=yes`: pass `--ignore-tls-errors` to both `gpauth` and
  `gpclient`.
- `enable_csd_trojan=yes` plus `csd_wrapper`: pass `--hip [script]`.
- `no_dtls=yes` or inherited `disable_udp=yes`: pass `--no-dtls`.

The service currently reads these VPN secrets:

- `gateway`: fixed gateway or direct-gateway target.
- `cookie`: optional `gpauth` JSON result for the diagnostic cookie-on-stdin
  flow, not a raw OpenConnect cookie.
- `key_pass`: optional private-key password.

## Testing plan

Build-time:

```sh
autoreconf -fi
./configure
make
```

On Debian/Ubuntu the core build dependency for libnm is `libnm-dev`; on Fedora
it is `NetworkManager-libnm-devel`. The GNOME editor additionally needs
`libnma-dev`, GTK 3 development files, and GTK 4 development files. GNOME
Settings is GTK 4 on current desktops, so the GTK 4 editor module is required
there.

Local core install/test:

```sh
./tools/install-core.sh
```

That script installs the service, helper, auth helper, D-Bus policy, sysusers
file, and VPN service metadata under `/usr`. It uses `sudo`, so it needs to be
run from a terminal where you can enter your password.

GNOME editor build/install:

```sh
./configure --prefix=/usr --libexecdir=/usr/libexec --with-gnome=yes --with-gtk4=yes --with-authdlg=yes
make
./tools/test-editor-plugin.sh
./tools/install-gui.sh
```

`tools/install-gui.sh` installs the core service files plus
`libnm-vpn-plugin-gpclient.so`, `libnm-vpn-plugin-gpclient-editor.so`, and
`libnm-gtk4-vpn-plugin-gpclient-editor.so`. After that, NetworkManager editors
that load libnm VPN plugins should offer `GlobalProtect (gpclient)` in the Add
VPN workflow. If it does not appear immediately, reopen the editor or restart
NetworkManager after disconnecting active VPNs.

For gpclient 2.5.x, the reliable GUI setup path is direct gateway mode:

1. Set `Gateway` to the gateway value that works with
   `gpclient connect --as-gateway`, for example `gateway.example.edu`.
2. Leave `Browser` as `default` unless you need a specific browser command.
3. Tick `Connect directly to gateway`.
4. Leave `Portal` empty unless you also want to keep the portal value in the
   profile for reference.

With direct gateway mode enabled, the auth helper uses `gpauth --gateway` and
the service runs `gpclient connect <gateway> --as-gateway --cookie-on-stdin`.
This avoids gpclient 2.5.x's portal-cookie gateway fallback limitation.

Portal mode is still available by leaving `Connect directly to gateway`
unchecked and setting `Portal` to the portal. In that mode, `Gateway` maps to
gpclient's `--gateway` argument, but it is not equivalent to direct gateway
authentication: the initial browser auth is still portal-scoped. With gpclient
2.5.x this can fail even when direct gateway mode succeeds. Newer gpclient
builds add `--auto-gateway`, which is a better fit for non-interactive
NetworkManager portal-mode activation.

After disconnecting any manually-started `gpclient` session:

```sh
./tools/run-core-test.sh vpn.example.edu gpclient-test default
```

The runner creates an `nmcli` VPN profile if needed, then starts it.
NetworkManager should then launch `nm-gpclient-service`, which launches
`gpclient`; gpclient should open browser auth as needed.

To configure direct gateway mode with `nmcli`:

```sh
nmcli connection modify gpclient-test \
  vpn.data "gateway=gateway.example.edu,as_gateway=yes,browser=default" \
  vpn.secrets ""
./tools/store-gateway-cookie.sh gpclient-test gateway.example.edu default
nmcli connection up gpclient-test
```

In the GUI, set `Gateway` to `gateway.example.edu` and tick
`Connect directly to gateway`. If `Gateway` is empty, direct mode falls back to
the value from `Portal`.

The cookie-storage helper is intended for diagnostics. It deliberately escapes
both backslashes and commas before calling `nmcli`. Without the backslash
escaping, a JSON username such as `DOMAIN\\user` can be collapsed into a JSON
Unicode escape and the gateway rejects the login as an invalid
username/password.

Unit/smoke testing without a real VPN:

1. Put a fake `gpauth` earlier in `PATH`.
2. Feed `nm-gpclient-auth-dialog` synthetic NetworkManager auth-helper stdin.
3. Assert the fake process receives the expected `gpauth` arguments and that the
   helper writes `gateway` and `cookie` key/value pairs on stdout.
4. For the service side, run `nm-gpclient-service` with a test bus name and a
   synthetic NM VPN activation, or add a small service test harness that calls
   the launch helper.
5. Assert the fake `gpclient` process receives:
   `connect`, server, `--cookie-on-stdin`, `--script`, `--interface`, and the
   expected gateway flags.
6. Run `./tools/test-editor-plugin.sh` to verify the libnm plugin loads and
   advertises the gpclient service type.

Integration testing with NetworkManager:

1. Install into a temporary prefix or package sandbox.
2. Install `nm-gpclient-service.name` into NetworkManager's VPN service
   directory and the D-Bus policy file.
3. Create an NM VPN connection with service type
   `org.freedesktop.NetworkManager.gpclient`.
4. Activate with `nmcli connection up <name>`.
5. Confirm the auth helper launches `gpauth` in the user session and opens the
   configured browser.
6. Confirm NetworkManager receives helper config and owns routes/DNS.

Next implementation checkpoint:

- Add a fake-gpclient test harness for service command construction.
- Expand the editor with certificate/key file selectors using libnma's
  certificate chooser widgets.
