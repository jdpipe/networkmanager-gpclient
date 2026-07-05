/* Minimal GTK editor module for NetworkManager-gpclient. */

#include "nm-default.h"
#include "nm-gpclient-editor-plugin.h"

#include <gtk/gtk.h>
#include <string.h>
#include <sys/wait.h>

typedef struct _GpclientEditor GpclientEditor;
typedef struct _GpclientEditorClass GpclientEditorClass;

struct _GpclientEditor {
	GObject parent;
	GtkWidget *widget;
	GtkWidget *portal_label;
	GtkWidget *portal_entry;
	GtkWidget *gateway_entry;
	GtkWidget *browser_combo;
	GtkWidget *browser_custom_entry;
	GtkWidget *direct_gateway_check;
	GtkWidget *no_dtls_check;
	GtkWidget *client_version_entry;
	GtkWidget *reported_os_combo;
	GtkWidget *ignore_tls_check;
	gboolean portal_mode_supported;
};

struct _GpclientEditorClass {
	GObjectClass parent;
};

static void gpclient_editor_interface_init(NMVpnEditorInterface *iface);
GType gpclient_editor_get_type(void);

#define GPCLIENT_EDITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), gpclient_editor_get_type(), GpclientEditor))

G_DEFINE_TYPE_EXTENDED(GpclientEditor,
                       gpclient_editor,
                       G_TYPE_OBJECT,
                       0,
                       G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR,
                                             gpclient_editor_interface_init))

static const char *
vpn_data(NMConnection *connection, const char *key)
{
	NMSettingVpn *s_vpn;

	if (!connection)
		return NULL;

	s_vpn = nm_connection_get_setting_vpn(connection);
	return s_vpn ? nm_setting_vpn_get_data_item(s_vpn, key) : NULL;
}

static gboolean
is_yes(const char *value)
{
	return value && !strcmp(value, "yes");
}

static char *
find_gpclient_binary(void)
{
	static const char *paths[] = {
		"/usr/bin/gpclient",
		"/usr/local/bin/gpclient",
		"/opt/bin/gpclient",
		NULL
	};
	guint i;
	char *path;

	for (i = 0; paths[i]; i++) {
		if (g_file_test(paths[i], G_FILE_TEST_EXISTS))
			return g_strdup(paths[i]);
	}

	path = g_find_program_in_path("gpclient");
	return path;
}

static gboolean
gpclient_supports_connect_arg(const char *arg)
{
	char *binary;
	char **argv;
	char *help_text = NULL;
	GError *error = NULL;
	gint exit_status = 0;
	gboolean ok;
	gboolean supported;

	binary = find_gpclient_binary();
	if (!binary)
		return FALSE;

	argv = g_new0(char *, 4);
	argv[0] = g_strdup(binary);
	argv[1] = g_strdup("connect");
	argv[2] = g_strdup("--help");

	ok = g_spawn_sync(NULL,
	                  argv,
	                  NULL,
	                  G_SPAWN_DEFAULT,
	                  NULL,
	                  NULL,
	                  &help_text,
	                  NULL,
	                  &exit_status,
	                  &error);
	g_strfreev(argv);
	g_free(binary);

	if (!ok) {
		g_clear_error(&error);
		g_free(help_text);
		return FALSE;
	}

	if (!WIFEXITED(exit_status) || WEXITSTATUS(exit_status) != 0) {
		g_free(help_text);
		return FALSE;
	}

	supported = help_text && g_strstr_len(help_text, -1, arg);
	g_free(help_text);
	return supported;
}

static const char *
entry_get_text(GtkWidget *entry)
{
#ifdef GPCLIENT_GTK4
	return gtk_editable_get_text(GTK_EDITABLE(entry));
#else
	return gtk_entry_get_text(GTK_ENTRY(entry));
#endif
}

static void
entry_set_text(GtkWidget *entry, const char *text)
{
#ifdef GPCLIENT_GTK4
	gtk_editable_set_text(GTK_EDITABLE(entry), text ? text : "");
#else
	gtk_entry_set_text(GTK_ENTRY(entry), text ? text : "");
#endif
}

static gboolean
check_get_active(GtkWidget *check)
{
#ifdef GPCLIENT_GTK4
	return gtk_check_button_get_active(GTK_CHECK_BUTTON(check));
#else
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check));
#endif
}

static void
check_set_active(GtkWidget *check, gboolean active)
{
#ifdef GPCLIENT_GTK4
	gtk_check_button_set_active(GTK_CHECK_BUTTON(check), active);
#else
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), active);
#endif
}

static void
box_append(GtkWidget *box, GtkWidget *child)
{
#ifdef GPCLIENT_GTK4
	gtk_box_append(GTK_BOX(box), child);
#else
	gtk_box_pack_start(GTK_BOX(box), child, FALSE, FALSE, 0);
#endif
}

static void
hide_from_editor(GtkWidget *widget)
{
#ifndef GPCLIENT_GTK4
	gtk_widget_set_no_show_all(widget, TRUE);
#endif
	gtk_widget_hide(widget);
}

static void
emit_changed(GtkWidget *widget, gpointer user_data)
{
	g_signal_emit_by_name(GPCLIENT_EDITOR(user_data), "changed");
}

static gboolean
browser_is_known(const char *browser)
{
	return browser &&
	       (!strcmp(browser, "default") ||
	        !strcmp(browser, "firefox") ||
	        !strcmp(browser, "chrome") ||
	        !strcmp(browser, "chromium") ||
	        !strcmp(browser, "remote"));
}

static void
browser_update_sensitivity(GpclientEditor *editor)
{
	const char *active_id;
	gboolean use_custom;

	active_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(editor->browser_combo));
	use_custom = active_id && !strcmp(active_id, "custom");

	gtk_widget_set_sensitive(editor->browser_custom_entry, use_custom);
}

static char *
browser_get_value(GpclientEditor *editor)
{
	const char *active_id;

	active_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(editor->browser_combo));
	if (active_id && !strcmp(active_id, "custom"))
		return g_strdup(entry_get_text(editor->browser_custom_entry));

	return g_strdup(active_id ? active_id : "");
}

static GtkWidget *
new_browser_combo(const char *browser)
{
	GtkWidget *combo;

	combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "default", _("Default"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "firefox", "Firefox");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "chrome", "Chrome");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "chromium", "Chromium");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "remote", _("Remote"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "custom", _("Custom"));
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), browser_is_known(browser) ? browser : "custom");
	return combo;
}

static void
browser_changed(GtkWidget *widget, gpointer user_data)
{
	GpclientEditor *editor = GPCLIENT_EDITOR(user_data);

	browser_update_sensitivity(editor);

	g_signal_emit_by_name(editor, "changed");
}

static void
direct_gateway_toggled(GtkWidget *widget, gpointer user_data)
{
	GpclientEditor *editor = GPCLIENT_EDITOR(user_data);
	gboolean direct_gateway;

	direct_gateway = check_get_active(widget);
	gtk_widget_set_sensitive(editor->portal_entry, editor->portal_mode_supported && !direct_gateway);
	gtk_widget_set_sensitive(editor->gateway_entry, TRUE);

	g_signal_emit_by_name(editor, "changed");
}

static GtkWidget *
add_row(GtkGrid *grid, int row, const char *label_text, GtkWidget *widget)
{
	GtkWidget *label;

	label = gtk_label_new(label_text);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand(widget, TRUE);
	gtk_grid_attach(grid, label, 0, row, 1, 1);
	gtk_grid_attach(grid, widget, 1, row, 1, 1);
	return label;
}

static GtkWidget *
new_entry(const char *text)
{
	GtkWidget *entry;

	entry = gtk_entry_new();
#ifndef GPCLIENT_GTK4
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
#endif
	entry_set_text(entry, text);
	return entry;
}

static NMVpnEditor *
gpclient_editor_new(NMConnection *connection, GError **error)
{
	GpclientEditor *editor;
	GtkWidget *grid;
	GtkWidget *combo;
	GtkWidget *content;
	const char *value;
	gboolean direct_gateway;
	int row = 0;

	editor = g_object_new(gpclient_editor_get_type(), NULL);
	if (!editor) {
		g_set_error(error,
		            NMV_EDITOR_PLUGIN_ERROR,
		            NMV_EDITOR_PLUGIN_ERROR_FAILED,
		            "%s",
		            "could not create gpclient editor");
		return NULL;
	}

	content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_top(content, 12);
	gtk_widget_set_margin_bottom(content, 12);
	gtk_widget_set_margin_start(content, 12);
	gtk_widget_set_margin_end(content, 12);

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	box_append(content, grid);

	editor->portal_mode_supported = gpclient_supports_connect_arg("--auto-gateway");
	direct_gateway = is_yes(vpn_data(connection, NM_OPENCONNECT_KEY_AS_GATEWAY));
	if (!editor->portal_mode_supported)
		direct_gateway = TRUE;

	value = vpn_data(connection, NM_OPENCONNECT_KEY_PORTAL);
	editor->portal_entry = new_entry(value);
	editor->portal_label = add_row(GTK_GRID(grid), row++, _("Portal"), editor->portal_entry);

	value = vpn_data(connection, NM_OPENCONNECT_KEY_GATEWAY);
	editor->gateway_entry = new_entry(value);
	add_row(GTK_GRID(grid), row++, _("Gateway"), editor->gateway_entry);

	editor->direct_gateway_check =
		gtk_check_button_new_with_label(_("Connect directly to gateway"));
	check_set_active(editor->direct_gateway_check, direct_gateway);
	if (editor->portal_mode_supported) {
		gtk_widget_set_tooltip_text(editor->direct_gateway_check,
		                            _("Authenticate to the gateway itself instead of authenticating to a portal and then selecting a gateway."));
	} else {
		gtk_widget_set_sensitive(editor->direct_gateway_check, FALSE);
		gtk_widget_set_tooltip_text(editor->direct_gateway_check,
		                            _("Portal mode requires a newer gpclient with --auto-gateway support. This installed gpclient supports direct gateway mode only."));
		hide_from_editor(editor->portal_label);
		hide_from_editor(editor->portal_entry);
	}
	gtk_grid_attach(GTK_GRID(grid), editor->direct_gateway_check, 1, row++, 1, 1);
	gtk_widget_set_sensitive(editor->portal_entry, editor->portal_mode_supported && !direct_gateway);

	value = vpn_data(connection, NM_OPENCONNECT_KEY_BROWSER);
	editor->browser_combo = new_browser_combo(value && value[0] ? value : "default");
	add_row(GTK_GRID(grid), row++, _("Browser"), editor->browser_combo);

	editor->browser_custom_entry = new_entry(value && value[0] && !browser_is_known(value) ? value : "");
	gtk_widget_set_tooltip_text(editor->browser_custom_entry,
	                            _("Used only when Browser is set to Custom. Enter a browser command or executable path."));
	add_row(GTK_GRID(grid), row++, _("Custom browser command"), editor->browser_custom_entry);
	browser_update_sensitivity(editor);

	value = vpn_data(connection, NM_OPENCONNECT_KEY_CLIENT_VERSION);
	editor->client_version_entry = new_entry(value);
	add_row(GTK_GRID(grid), row++, _("Client version"), editor->client_version_entry);

	combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "default", _("Default"));
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "Linux", "Linux");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "Windows", "Windows");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "Mac", "Mac");
	value = vpn_data(connection, NM_OPENCONNECT_KEY_REPORTED_OS);
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), value && value[0] ? value : "default");
	editor->reported_os_combo = combo;
	add_row(GTK_GRID(grid), row++, _("Reported OS"), editor->reported_os_combo);

	editor->no_dtls_check = gtk_check_button_new_with_label(_("Disable DTLS/ESP"));
	gtk_widget_set_tooltip_text(editor->no_dtls_check,
	                            _("Use only the TLS tunnel and disable the UDP data channel. This can help on networks that block or break UDP, but may reduce VPN performance."));
	check_set_active(editor->no_dtls_check, is_yes(vpn_data(connection, NM_OPENCONNECT_KEY_NO_DTLS)));
	gtk_grid_attach(GTK_GRID(grid), editor->no_dtls_check, 1, row++, 1, 1);

	editor->ignore_tls_check = gtk_check_button_new_with_label(_("Ignore TLS errors"));
	check_set_active(editor->ignore_tls_check, is_yes(vpn_data(connection, NM_OPENCONNECT_KEY_IGNORE_TLS_ERRORS)));
	gtk_grid_attach(GTK_GRID(grid), editor->ignore_tls_check, 1, row++, 1, 1);

	g_signal_connect(editor->portal_entry, "changed", G_CALLBACK(emit_changed), editor);
	g_signal_connect(editor->gateway_entry, "changed", G_CALLBACK(emit_changed), editor);
	g_signal_connect(editor->direct_gateway_check, "toggled", G_CALLBACK(direct_gateway_toggled), editor);
	g_signal_connect(editor->browser_combo, "changed", G_CALLBACK(browser_changed), editor);
	g_signal_connect(editor->browser_custom_entry, "changed", G_CALLBACK(emit_changed), editor);
	g_signal_connect(editor->client_version_entry, "changed", G_CALLBACK(emit_changed), editor);
	g_signal_connect(editor->reported_os_combo, "changed", G_CALLBACK(emit_changed), editor);
	g_signal_connect(editor->no_dtls_check, "toggled", G_CALLBACK(emit_changed), editor);
	g_signal_connect(editor->ignore_tls_check, "toggled", G_CALLBACK(emit_changed), editor);

	editor->widget = content;
	g_object_ref_sink(editor->widget);
#ifndef GPCLIENT_GTK4
	gtk_widget_show_all(editor->widget);
#endif

	return NM_VPN_EDITOR(editor);
}

static GObject *
editor_get_widget(NMVpnEditor *iface)
{
	return G_OBJECT(GPCLIENT_EDITOR(iface)->widget);
}

static gboolean
editor_update_connection(NMVpnEditor *iface, NMConnection *connection, GError **error)
{
	GpclientEditor *editor = GPCLIENT_EDITOR(iface);
	NMSettingVpn *s_vpn;
	const char *portal;
	const char *gateway;
	char *browser;
	const char *client_version;
	const char *reported_os;
	gboolean direct_gateway;

	portal = entry_get_text(editor->portal_entry);
	gateway = entry_get_text(editor->gateway_entry);
	browser = browser_get_value(editor);
	client_version = entry_get_text(editor->client_version_entry);
	reported_os = gtk_combo_box_get_active_id(GTK_COMBO_BOX(editor->reported_os_combo));
	direct_gateway = check_get_active(editor->direct_gateway_check);

	if (direct_gateway) {
		if ((!gateway || !gateway[0]) && (!portal || !portal[0])) {
			g_set_error(error,
			            NMV_EDITOR_PLUGIN_ERROR,
			            NMV_EDITOR_PLUGIN_ERROR_MISSING_PROPERTY,
			            NM_OPENCONNECT_KEY_GATEWAY);
			g_free(browser);
			return FALSE;
		}
	} else if (!portal || !portal[0]) {
		g_set_error(error,
		            NMV_EDITOR_PLUGIN_ERROR,
		            NMV_EDITOR_PLUGIN_ERROR_MISSING_PROPERTY,
		            NM_OPENCONNECT_KEY_PORTAL);
		g_free(browser);
		return FALSE;
	}

	s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());
	g_object_set(s_vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_VPN_SERVICE_TYPE_OPENCONNECT, NULL);

	if (direct_gateway) {
		nm_setting_vpn_add_data_item(s_vpn,
		                             NM_OPENCONNECT_KEY_GATEWAY,
		                             gateway && gateway[0] ? gateway : portal);
		nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_AS_GATEWAY, "yes");
	} else {
		nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_PORTAL, portal);
		if (gateway && gateway[0])
			nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_GATEWAY, gateway);
		else
			nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_AUTO_GATEWAY, "yes");
	}

	if (browser && browser[0])
		nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_BROWSER, browser);
	if (client_version && client_version[0])
		nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_CLIENT_VERSION, client_version);
	if (reported_os && reported_os[0] && strcmp(reported_os, "default"))
		nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_REPORTED_OS, reported_os);
	if (check_get_active(editor->no_dtls_check))
		nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_NO_DTLS, "yes");
	if (check_get_active(editor->ignore_tls_check))
		nm_setting_vpn_add_data_item(s_vpn, NM_OPENCONNECT_KEY_IGNORE_TLS_ERRORS, "yes");

	nm_setting_set_secret_flags(NM_SETTING(s_vpn),
	                            NM_OPENCONNECT_KEY_COOKIE,
	                            NM_SETTING_SECRET_FLAG_NOT_SAVED,
	                            NULL);
	nm_setting_set_secret_flags(NM_SETTING(s_vpn),
	                            NM_OPENCONNECT_KEY_GATEWAY,
	                            NM_SETTING_SECRET_FLAG_NOT_SAVED,
	                            NULL);

	nm_connection_add_setting(connection, NM_SETTING(s_vpn));
	g_free(browser);
	return TRUE;
}

static void
gpclient_editor_dispose(GObject *object)
{
	GpclientEditor *editor = GPCLIENT_EDITOR(object);

	g_clear_object(&editor->widget);
	G_OBJECT_CLASS(gpclient_editor_parent_class)->dispose(object);
}

static void
gpclient_editor_class_init(GpclientEditorClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = gpclient_editor_dispose;
}

static void
gpclient_editor_init(GpclientEditor *editor)
{
}

static void
gpclient_editor_interface_init(NMVpnEditorInterface *iface)
{
	iface->get_widget = editor_get_widget;
	iface->update_connection = editor_update_connection;
}

G_MODULE_EXPORT NMVpnEditor *
nm_vpn_editor_factory_gpclient(NMVpnEditorPlugin *editor_plugin,
                               NMConnection *connection,
                               GError **error)
{
	return gpclient_editor_new(connection, error);
}
