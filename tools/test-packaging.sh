#!/bin/sh
# Verify packaging paths without installing files or touching NetworkManager.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'status=$?; if [ "$status" -ne 0 ]; then tail -60 "$work/build.log" >&2; fi; rm -rf "$work"' EXIT

make distdir distdir="$work/source" >"$work/build.log" 2>&1
mkdir -p "$work/store/client/bin" "$work/store/auth/bin" "$work/store/kmod/bin" "$work/shadow" "$work/build"
export PACKAGING_LOG="$work/calls"
cat >"$work/store/client/bin/gpclient" <<'CLIENT'
#!/bin/sh
printf 'gpclient:%s\n' "$*" >>"$PACKAGING_LOG"
case "$*" in
    --version) echo 'gpclient 3.2.0' ;;
    'connect --help') echo '--auto-gateway' ;;
    *) exit 99 ;;
esac
CLIENT
cat >"$work/store/auth/bin/gpauth" <<'AUTH'
#!/bin/sh
printf 'gpauth:%s\n' "$*" >>"$PACKAGING_LOG"
if [ "$1" = --version ]; then echo 'gpauth 3.2.0'; exit 0; fi
printf '%s\n' '{"success":{"preloginCookie":"synthetic-packaging-cookie","username":"test-user"}}'
AUTH
cat >"$work/store/kmod/bin/modprobe" <<'MODULE'
#!/bin/sh
printf 'modprobe:%s\n' "$*" >>"$PACKAGING_LOG"
exit "${MODULE_EXIT:-0}"
MODULE
for name in gpclient gpauth modprobe; do
    printf '#!/bin/sh\necho unexpected-PATH-lookup >>"$PACKAGING_LOG"\nexit 99\n' >"$work/shadow/$name"
done
real_pkg_config=$(command -v pkg-config)
cat >"$work/shadow/pkg-config" <<PKG
#!/bin/sh
case "\$*" in
    '--variable=plugindir libnm')
        [ "\${EMPTY_NM_DIRS:-no}" = yes ] || echo '$work/store/libnm/lib/NetworkManager'
        exit 0 ;;
    '--variable=vpnservicedir libnm')
        [ "\${EMPTY_NM_DIRS:-no}" = yes ] || echo '$work/store/libnm/lib/NetworkManager/VPN'
        exit 0 ;;
esac
exec '$real_pkg_config' "\$@"
PKG
chmod +x "$work"/store/*/bin/* "$work"/shadow/*
export PATH="$work/shadow:$PATH"
cd "$work/build"
"$work/source/configure" --prefix="$work/client-prefix" --libexecdir="$work/client-prefix/libexec" \
    --with-gnome=yes --with-gtk4=yes --enable-more-warnings=error --enable-absolute-paths \
    --with-gpclient="$work/store/client/bin/gpclient" \
    --with-gpauth="$work/store/auth/bin/gpauth" \
    --with-modprobe="$work/store/kmod/bin/modprobe" \
    --with-gpclient-lock-file="$work/vpn.lock" >>"$work/build.log" 2>&1
make -j2 >>"$work/build.log" 2>&1
printf '%s\n' DATA_KEY=gateway DATA_VAL=gateway.example.invalid DATA_KEY=as_gateway DATA_VAL=yes DONE |
    ./auth-dialog/nm-gpclient-auth-dialog --uuid packaging-test --name packaging-test \
    --service org.freedesktop.NetworkManager.gpclient --allow-interaction >"$work/auth-output"
grep -Fq synthetic-packaging-cookie "$work/auth-output"
grep -Fxq 'gpauth:--gateway gateway.example.invalid --browser default' "$PACKAGING_LOG"

cat >"$work/check-service.c" <<CHECK
int gpclient_service_main (int argc, char **argv);
#define main gpclient_service_main
#include "$work/source/src/nm-gpclient-service.c"
#undef main
int main (void) {
    GPid pid = 0;
    GError *error = NULL;
    gl.log_level = LOG_DEBUG;
    g_assert (nm_gpclient_check_version (NM_GPCLIENT_BINARY, "gpclient", &error));
    g_assert_no_error (error);
    g_assert (g_file_set_contents (NM_GPCLIENT_LOCK_FILE, "2147483647\\n", -1, &error));
    g_assert (read_gpclient_lock_pid (&pid));
    g_assert_cmpint (pid, ==, 2147483647);
    g_assert (prepare_gpclient_lock (&error));
    g_assert_no_error (error);
    g_assert (!g_file_test (NM_GPCLIENT_LOCK_FILE, G_FILE_TEST_EXISTS));
    load_tun_module ();
    g_setenv ("MODULE_EXIT", "7", TRUE);
    load_tun_module ();
    return 0;
}
CHECK
cc -I. -I"$work/source/shared" -I"$work/source/src" \
    -DLIBEXECDIR=\"/unused\" -DNM_OPENCONNECT_LOCALEDIR=\"/unused\" \
    "$work/check-service.c" "$work/source/shared/nm-utils/nm-shared-utils.c" \
    $(pkg-config --cflags --libs libnm gio-2.0) -o "$work/check-service" >>"$work/build.log" 2>&1
"$work/check-service" >"$work/service-output"
grep -Fq 'TUN module loader failed' "$work/service-output"
test "$(grep -Fxc modprobe:tun "$PACKAGING_LOG")" -eq 2

cat >"$work/check-editor.c" <<CHECK
#include "$work/source/properties/nm-gpclient-editor.c"
int main (void) { g_assert (gpclient_supports_connect_arg ("--auto-gateway")); return 0; }
CHECK
cc -I. -I"$work/source/shared" -DNETWORKMANAGER_COMPILATION=NM_NETWORKMANAGER_COMPILATION_LIB_EDITOR \
    "$work/check-editor.c" $(pkg-config --cflags --libs gtk+-3.0 libnm) \
    -o "$work/check-editor" >>"$work/build.log" 2>&1
"$work/check-editor"
grep -Fxq 'gpclient:connect --help' "$PACKAGING_LOG"
! grep -Fq unexpected-PATH-lookup "$PACKAGING_LOG"

make install DESTDIR="$work/stage" >>"$work/build.log" 2>&1
test -x "$work/stage$work/client-prefix/libexec/nm-gpclient-browser-helper"
test -f "$work/stage$work/store/libnm/lib/NetworkManager/libnm-vpn-plugin-gpclient.so"
grep -Fxq "plugin=$work/store/libnm/lib/NetworkManager/libnm-vpn-plugin-gpclient.so" nm-gpclient-service.name
printf '%s\n' 'PASS: configured executables, module-loader failure, lock cleanup, pkg-config directories, and staged installation'

# Disabling absolute metadata paths must not empty the installation directory.
mkdir "$work/relative"
cd "$work/relative"
"$work/source/configure" --without-gnome --without-authdlg --without-modprobe \
    --disable-absolute-paths --with-nm-plugin-dir="$work/custom-plugins" \
    --with-nm-vpn-service-dir="$work/custom-services" >>"$work/build.log" 2>&1
make nm-gpclient-service.name >>"$work/build.log" 2>&1
grep -Fxq 'plugin=libnm-vpn-plugin-gpclient.so' nm-gpclient-service.name
grep -Fxq "NM_PLUGIN_DIR = $work/custom-plugins" Makefile
grep -Fxq "NM_VPN_SERVICE_DIR = $work/custom-services" Makefile
grep -Fxq '#define NM_MODPROBE_DEFAULT ""' config.h

mkdir "$work/fallback"
cd "$work/fallback"
EMPTY_NM_DIRS=yes "$work/source/configure" --prefix="$work/fallback-prefix" \
    --without-gnome --without-authdlg --without-modprobe --enable-absolute-paths >>"$work/build.log" 2>&1
make -j2 >>"$work/build.log" 2>&1
grep -Fxq "plugin=$work/fallback-prefix/lib/NetworkManager/libnm-vpn-plugin-gpclient.so" nm-gpclient-service.name
printf '%s\n' 'PASS: explicit directory overrides, relative metadata, empty pkg-config fallback, and service-only build'
