# NetworkManager-gpclient Notes

The main project documentation is now in [`README.md`](README.md). That file
explains what this project is, how it differs from
`NetworkManager-openconnect`, how to install it, and which `gpclient` version
has been tested.

This file is kept as a short developer note for prototype-specific details that
may change quickly.

## Service Settings

The service currently reads these VPN data items:

- `portal`: optional portal server.
- `gateway`: gateway or direct-gateway target.
- `auto_gateway=yes`: portal mode without a fixed gateway. This needs newer
  `gpclient` support to be useful.
- `as_gateway=yes`: treat `gateway` as the direct GlobalProtect gateway.
- `browser`: browser mode passed to `gpauth`.
- `mtu`: passed to `gpclient --mtu`.
- `usercert`: passed to `gpauth`/`gpclient --certificate`.
- `userkey`: passed to `gpauth`/`gpclient --sslkey`.
- `client_version`: passed as `--client-version`.
- `reported_os`: passed as `--os`.
- `fix_openssl=yes`: passed as `--fix-openssl`.
- `ignore_tls_errors=yes`: passed as `--ignore-tls-errors`.
- `enable_csd_trojan=yes` plus `csd_wrapper`: passed as `--hip [script]`.
- `no_dtls=yes` or inherited `disable_udp=yes`: passed as `--no-dtls`.

The service currently reads these VPN secrets:

- `cookie`: `gpauth` JSON result for the `--cookie-on-stdin` flow.
- `gateway`: fixed gateway secret from the auth helper.
- `key_pass`: optional private-key password.

## Test Commands

Local smoke tests:

```sh
make check
./tools/test-auth-dialog.sh
./tools/test-auth-dialog-quit.sh
./tools/test-editor-plugin.sh
```

Installed editor plugin check:

```sh
./tools/test-installed-editor-plugin.sh
```

Manual NetworkManager activation test:

```sh
nmcli connection up <profile-name>
journalctl -b --no-pager --since '10 minutes ago' | \
  rg 'nm-gpclient|gpclient|NetworkManager|vpn'
```

If an earlier failed run leaves an assumed raw TUN connection behind, the
manual cleanup is:

```sh
nmcli connection down vpn0
```

The service now attempts to clean up that stale `vpn0` state automatically when
it is owned by the dedicated `nm-gpclient` user. For `gpclient` 2.6.x
compatibility, the service starts the `gpclient` child as root and supplies the
`SUDO_*` desktop-user environment that gpclient expects when it relaunches
browser authentication. It also refuses to start over a live `gpclient` lock
file and removes stale `/var/run/gpclient.lock` files before launch.

## Next Work

- Add a fake-`gpclient` service test harness for command construction and
  disconnect cleanup.
- Improve client-certificate handling before treating certificate auth as
  production-ready.
- Investigate a deeper fix for NetworkManager assuming the raw TUN device,
  possibly through a different tunnel integration model.
