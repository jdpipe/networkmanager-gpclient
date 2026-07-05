#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cat >"$tmpdir/test-editor-plugin.c" <<'EOF'
#include <NetworkManager.h>
#include <gmodule.h>
#include <stdio.h>

typedef NMVpnEditorPlugin *(*FactoryFunc)(GError **error);

int
main(int argc, char **argv)
{
	GModule *module;
	FactoryFunc factory;
	NMVpnEditorPlugin *plugin;
	GError *error = NULL;
	char *name = NULL;
	char *service = NULL;

	if (argc != 2)
		return 2;

	module = g_module_open(argv[1], G_MODULE_BIND_LAZY);
	if (!module) {
		fprintf(stderr, "%s\n", g_module_error());
		return 1;
	}

	if (!g_module_symbol(module, "nm_vpn_editor_plugin_factory", (gpointer *) &factory)) {
		fprintf(stderr, "%s\n", g_module_error());
		return 1;
	}

	plugin = factory(&error);
	if (!plugin) {
		fprintf(stderr, "%s\n", error ? error->message : "factory returned NULL");
		return 1;
	}

	g_object_get(plugin,
	             NM_VPN_EDITOR_PLUGIN_NAME, &name,
	             NM_VPN_EDITOR_PLUGIN_SERVICE, &service,
	             NULL);

	printf("name=%s\nservice=%s\n", name, service);

	if (!g_str_equal(service, "org.freedesktop.NetworkManager.gpclient"))
		return 1;

	g_free(name);
	g_free(service);
	g_object_unref(plugin);
	g_module_close(module);
	return 0;
}
EOF

gcc "$tmpdir/test-editor-plugin.c" -o "$tmpdir/test-editor-plugin" \
	$(pkg-config --cflags --libs libnm gmodule-2.0)

plugin="${1:-properties/.libs/libnm-vpn-plugin-gpclient.so}"
"$tmpdir/test-editor-plugin" "$plugin"
