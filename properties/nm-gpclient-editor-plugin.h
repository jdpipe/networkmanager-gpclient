/* Minimal NetworkManager editor plugin declarations for gpclient. */

#ifndef __NM_GPCLIENT_EDITOR_PLUGIN_H__
#define __NM_GPCLIENT_EDITOR_PLUGIN_H__

#define GPCLIENT_TYPE_EDITOR_PLUGIN (gpclient_editor_plugin_get_type())
#define GPCLIENT_EDITOR_PLUGIN(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), GPCLIENT_TYPE_EDITOR_PLUGIN, GpclientEditorPlugin))
#define GPCLIENT_IS_EDITOR_PLUGIN(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), GPCLIENT_TYPE_EDITOR_PLUGIN))

typedef struct _GpclientEditorPlugin GpclientEditorPlugin;
typedef struct _GpclientEditorPluginClass GpclientEditorPluginClass;

struct _GpclientEditorPlugin {
	GObject parent;
};

struct _GpclientEditorPluginClass {
	GObjectClass parent;
};

GType gpclient_editor_plugin_get_type(void);

typedef NMVpnEditor *(*NMVpnEditorFactory)(NMVpnEditorPlugin *editor_plugin,
                                           NMConnection *connection,
                                           GError **error);

NMVpnEditor *nm_vpn_editor_factory_gpclient(NMVpnEditorPlugin *editor_plugin,
                                            NMConnection *connection,
                                            GError **error);

#endif /* __NM_GPCLIENT_EDITOR_PLUGIN_H__ */
