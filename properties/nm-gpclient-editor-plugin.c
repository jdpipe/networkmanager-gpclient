/* Minimal non-GTK NetworkManager editor plugin loader for gpclient. */

#include "nm-default.h"
#include "nm-gpclient-editor-plugin.h"

#include "nm-utils/nm-vpn-plugin-utils.h"

#include <gmodule.h>
#include <string.h>

#define GPCLIENT_PLUGIN_NAME _("GlobalProtect (gpclient)")
#define GPCLIENT_PLUGIN_DESC _("GlobalProtect VPN using gpclient browser authentication")

enum {
	PROP_0,
	PROP_NAME,
	PROP_DESC,
	PROP_SERVICE
};

static void gpclient_editor_plugin_interface_init(NMVpnEditorPluginInterface *iface);

G_DEFINE_TYPE_EXTENDED(GpclientEditorPlugin,
                       gpclient_editor_plugin,
                       G_TYPE_OBJECT,
                       0,
                       G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR_PLUGIN,
                                             gpclient_editor_plugin_interface_init))

static NMVpnEditor *
call_editor_factory(gpointer factory,
                    NMVpnEditorPlugin *editor_plugin,
                    NMConnection *connection,
                    gpointer user_data,
                    GError **error)
{
	return ((NMVpnEditorFactory) factory)(editor_plugin, connection, error);
}

static NMVpnEditor *
plugin_get_editor(NMVpnEditorPlugin *plugin, NMConnection *connection, GError **error)
{
	gpointer gtk3_only_symbol = NULL;
	GModule *self_module;
	const char *editor;

	g_return_val_if_fail(GPCLIENT_IS_EDITOR_PLUGIN(plugin), NULL);
	g_return_val_if_fail(NM_IS_CONNECTION(connection), NULL);
	g_return_val_if_fail(!error || !*error, NULL);

	self_module = g_module_open(NULL, 0);
	if (self_module) {
		g_module_symbol(self_module, "gtk_container_add", &gtk3_only_symbol);
		g_module_close(self_module);
	}

	if (gtk3_only_symbol)
		editor = "libnm-vpn-plugin-gpclient-editor.so";
	else
		editor = "libnm-gtk4-vpn-plugin-gpclient-editor.so";

	return nm_vpn_plugin_utils_load_editor(editor,
	                                       "nm_vpn_editor_factory_gpclient",
	                                       call_editor_factory,
	                                       plugin,
	                                       connection,
	                                       NULL,
	                                       error);
}

static NMVpnEditorPluginCapability
plugin_get_capabilities(NMVpnEditorPlugin *plugin)
{
	return NM_VPN_EDITOR_PLUGIN_CAPABILITY_NONE;
}

static void
plugin_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string(value, GPCLIENT_PLUGIN_NAME);
		break;
	case PROP_DESC:
		g_value_set_string(value, GPCLIENT_PLUGIN_DESC);
		break;
	case PROP_SERVICE:
		g_value_set_string(value, NM_VPN_SERVICE_TYPE_OPENCONNECT);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
gpclient_editor_plugin_class_init(GpclientEditorPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->get_property = plugin_get_property;
	g_object_class_override_property(object_class, PROP_NAME, NM_VPN_EDITOR_PLUGIN_NAME);
	g_object_class_override_property(object_class, PROP_DESC, NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
	g_object_class_override_property(object_class, PROP_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}

static void
gpclient_editor_plugin_init(GpclientEditorPlugin *plugin)
{
}

static void
gpclient_editor_plugin_interface_init(NMVpnEditorPluginInterface *iface)
{
	iface->get_editor = plugin_get_editor;
	iface->get_capabilities = plugin_get_capabilities;
}

G_MODULE_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory(GError **error)
{
	if (error)
		g_return_val_if_fail(*error == NULL, NULL);

	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");

	return g_object_new(GPCLIENT_TYPE_EDITOR_PLUGIN, NULL);
}
