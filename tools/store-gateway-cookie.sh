#!/bin/sh
set -eu

cat >&2 <<'EOF'
This helper is retired: gpauth results must not be stored in a VPN profile.
Set cookie-flags=2,gateway-flags=2, remove saved cookie/gateway secrets, and
connect through a running desktop secret agent to obtain fresh authentication.
See README.md, "Hand-created profiles", for creation and repair commands.
EOF
exit 1
