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
- `no_dtls`: defaults to `yes`, passing `--no-dtls` for HTTPS-only transport.
  Set `no_dtls=no` to enable DTLS/ESP. If `no_dtls` is absent, the inherited
  `disable_udp` value is used; if both keys are absent, HTTPS-only is used.

The service currently reads these VPN secrets:

- `cookie`: `gpauth` JSON result for the `--cookie-on-stdin` flow.
- `gateway`: fixed gateway secret from the auth helper.
- `key_pass`: optional private-key password.

Both `cookie` and `gateway` must have `NM_SETTING_SECRET_FLAG_NOT_SAVED`:
set `cookie-flags=2,gateway-flags=2` in `vpn.data` for hand-created profiles.
See [the manual profile examples](README.md#hand-created-profiles), including
how to repair profiles that already contain saved authentication results.
The service enforces this in both `NeedSecrets` and `Connect`; malformed or
missing flags fail with an actionable error. Flags `3` (`NOT_SAVED` plus
`AGENT_OWNED`) are also accepted; `NOT_REQUIRED` is rejected. Empty cookies
are treated as missing credentials.

## Test Commands

Local smoke tests:

```sh
make check
./tools/test-auth-dialog.sh
./tools/test-auth-dialog-quit.sh
./tools/test-reconnect.sh
./tools/test-editor-plugin.sh
```

`make check` always runs `test-secret-flags`, covering both service callbacks
with absent, malformed, saveable, and contradictory flags, with and without
cached credentials. It also checks that safe flags allow fresh credentials
through and that missing/empty cookies still request authentication. These
checks run without a system bus, a tunnel, or an auth dialog.

It also includes `test-reconnect.sh` when the auth dialog is enabled
(requires libnm >= 1.32 for secret filtering; skipped with older libnm).
This unprivileged component regression captures the test-profile script's
`vpn.data`, uses libnm's system-secret serialisation to model persistence,
and exercises the service's `NeedSecrets` callback and real auth dialog with
a fake `gpauth`. It models a second activation after discarding the first
token, in portal and direct-gateway modes, and verifies fresh credentials
survive D-Bus serialisation. It also checks that profiles which retain either
secret without the required flags are rejected. It does not exercise actual
token expiry, a tunnel connection, or a desktop secret agent/keyring.

For an installed end-to-end check, create a new direct-gateway profile using
the README command, connect through the desktop agent, then disconnect. Wait
past the gateway's SAML/prelogin-token lifetime and activate the same profile
again. Both activations must invoke `gpauth` and connect successfully; browser
SSO need not show a login prompt. After disconnect, verify that neither
`cookie` nor `gateway` remains in `vpn.secrets` or the agent's keyring. Do not
paste real authentication values into test logs.

Secret lifetime belongs to NetworkManager and its agent. The service receives
separate deserialised connections for `NeedSecrets` and `Connect`; clearing
one of those copies cannot repair persistent settings or an agent cache.
Clearing before consuming `Connect` secrets would also discard the fresh
authentication needed for that activation. No service-side clearing is used.

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
