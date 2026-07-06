/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *   Copyright © 2008 - 2009 Intel Corporation.
 *
 * Based on nm-vpnc-service.c:
 *   Copyright © 2005 - 2008 Red Hat, Inc.
 *   Copyright © 2007 - 2008 Novell, Inc.
 */

#include "nm-default.h"

#include "nm-gpclient-service.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <pwd.h>
#include <grp.h>
#include <locale.h>

#include "nm-utils/nm-shared-utils.h"
#include "nm-utils/nm-vpn-plugin-macros.h"

#if !defined(DIST_VERSION)
# define DIST_VERSION VERSION
#endif

G_DEFINE_TYPE (NMOpenconnectPlugin, nm_openconnect_plugin, NM_TYPE_VPN_SERVICE_PLUGIN)

typedef struct {
	GPid pid;
	char *tun_name;
	gboolean disconnect_requested;
} NMOpenconnectPluginPrivate;

#define NM_OPENCONNECT_PLUGIN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_OPENCONNECT_PLUGIN, NMOpenconnectPluginPrivate))

static const char *gpclient_binary_paths[] =
{
	"/usr/bin/gpclient",
	"/usr/local/bin/gpclient",
	"/opt/bin/gpclient",
	NULL
};

#define NM_GPCLIENT_HELPER_PATH LIBEXECDIR"/nm-gpclient-service-helper"
#define NM_GPCLIENT_BROWSER_HELPER_PATH LIBEXECDIR"/nm-gpclient-browser-helper"
#define GPCLIENT_LOCK_FILE "/var/run/gpclient.lock"

static gboolean
gpclient_supports_connect_arg (const char *binary, const char *arg)
{
	static GHashTable *help_cache = NULL;
	char **argv;
	char *help_text = NULL;
	GError *error = NULL;
	gint exit_status = 0;
	gboolean ok;
	gboolean supported;

	if (!help_cache)
		help_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	help_text = g_hash_table_lookup (help_cache, binary);
	if (!help_text) {
		argv = g_new0 (char *, 4);
		argv[0] = g_strdup (binary);
		argv[1] = g_strdup ("connect");
		argv[2] = g_strdup ("--help");

		ok = g_spawn_sync (NULL,
		                   argv,
		                   NULL,
		                   G_SPAWN_DEFAULT,
		                   NULL,
		                   NULL,
		                   &help_text,
		                   NULL,
		                   &exit_status,
		                   &error);
		g_strfreev (argv);

		if (!ok) {
			g_warning ("failed to inspect gpclient options: %s", error->message);
			g_clear_error (&error);
			return FALSE;
		}
		if (!WIFEXITED (exit_status) || WEXITSTATUS (exit_status) != 0) {
			g_free (help_text);
			return FALSE;
		}

		g_hash_table_insert (help_cache, g_strdup (binary), help_text);
	}

	supported = help_text && g_strstr_len (help_text, -1, arg);
	return supported;
}

typedef struct {
	const char *name;
	GType type;
	gint int_min;
	gint int_max;
} ValidProperty;

static const ValidProperty valid_properties[] = {
	{ NM_OPENCONNECT_KEY_GATEWAY,     G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_CACERT,      G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_AUTHTYPE,    G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_USERCERT,    G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_PRIVKEY,     G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_KEY_PASS,    G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_MTU,         G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_CLIENT_VERSION, G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_HOST_ID,     G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_FIX_OPENSSL, G_TYPE_BOOLEAN, 0, 0 },
	{ NM_OPENCONNECT_KEY_IGNORE_TLS_ERRORS, G_TYPE_BOOLEAN, 0, 0 },
	{ NM_OPENCONNECT_KEY_PEM_PASSPHRASE_FSID, G_TYPE_BOOLEAN, 0, 0 },
	{ NM_OPENCONNECT_KEY_PREVENT_INVALID_CERT, G_TYPE_BOOLEAN, 0, 0 },
	{ NM_OPENCONNECT_KEY_DISABLE_UDP, G_TYPE_BOOLEAN, 0, 0 },
	{ NM_OPENCONNECT_KEY_PROTOCOL,    G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_PROXY,       G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_CSD_ENABLE,  G_TYPE_BOOLEAN, 0, 0 },
	{ NM_OPENCONNECT_KEY_CSD_WRAPPER, G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_TOKEN_MODE,  G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_TOKEN_SECRET, G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_REPORTED_OS, G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_MCACERT,     G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_MCAKEY,      G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_MCA_PASS,    G_TYPE_STRING, 0, 0 },
	{ NULL,                           G_TYPE_NONE, 0, 0 }
};

static const ValidProperty valid_secrets[] = {
	{ NM_OPENCONNECT_KEY_COOKIE,  G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_GATEWAY, G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_GWCERT,  G_TYPE_STRING, 0, 0 },
	{ NM_OPENCONNECT_KEY_RESOLVE, G_TYPE_STRING, 0, 0 },
	{ NULL,                       G_TYPE_NONE, 0, 0 }
};

typedef struct ValidateInfo {
	const ValidProperty *table;
	GError **error;
	gboolean have_items;
} ValidateInfo;

static struct {
	uid_t tun_owner;
	gid_t tun_group;
	gboolean debug;
	int log_level;
	GMainLoop *loop;
} gl/*obal*/;

/*****************************************************************************/

#define _NMLOG(level, ...) \
	G_STMT_START { \
		if (gl.log_level >= (level)) { \
			g_print ("nm-gpclient[%ld] %-7s " _NM_UTILS_MACRO_FIRST (__VA_ARGS__) "\n", \
			         (long) getpid (), \
			         nm_utils_syslog_to_str (level) \
			         _NM_UTILS_MACRO_REST (__VA_ARGS__)); \
		} \
	} G_STMT_END

static gboolean
_LOGD_enabled (void)
{
	return gl.log_level >= LOG_INFO;
}

#define _LOGD(...) _NMLOG(LOG_INFO,    __VA_ARGS__)
#define _LOGI(...) _NMLOG(LOG_NOTICE,  __VA_ARGS__)
#define _LOGW(...) _NMLOG(LOG_WARNING, __VA_ARGS__)

/*****************************************************************************/

static void
validate_one_property (const char *key, const char *value, gpointer user_data)
{
	ValidateInfo *info = (ValidateInfo *) user_data;
	int i;

	if (*(info->error))
		return;

	info->have_items = TRUE;

	/* 'name' is the setting name; always allowed but unused */
	if (!strcmp (key, NM_SETTING_NAME))
		return;

	for (i = 0; info->table[i].name; i++) {
		ValidProperty prop = info->table[i];
		long int tmp;

		if (strcmp (prop.name, key))
			continue;

		switch (prop.type) {
		case G_TYPE_STRING:
			return; /* valid */
		case G_TYPE_INT:
			errno = 0;
			tmp = strtol (value, NULL, 10);
			if (errno == 0 && tmp >= prop.int_min && tmp <= prop.int_max)
				return; /* valid */

			g_set_error (info->error,
			             NM_VPN_PLUGIN_ERROR,
			             NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
			             _("invalid integer property “%s” or out of range [%d -> %d]"),
			             key, prop.int_min, prop.int_max);
			break;
		case G_TYPE_BOOLEAN:
			if (!strcmp (value, "yes") || !strcmp (value, "no"))
				return; /* valid */

			g_set_error (info->error,
			             NM_VPN_PLUGIN_ERROR,
			             NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
			             _("invalid boolean property “%s” (not yes or no)"),
			             key);
			break;
		default:
			g_set_error (info->error,
			             NM_VPN_PLUGIN_ERROR,
			             NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
			             _("unhandled property “%s” type %s"),
			             key, g_type_name (prop.type));
			break;
		}
	}

	/* Did not find the property from valid_properties or the type did not match */
	if (!info->table[i].name && strncmp(key, "form:", 5)) {
		_LOGW ("property '%s' unknown", key);
		if (0)
		g_set_error (info->error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             _("property “%s” invalid or not supported"),
		             key);
	}
}

static gboolean
nm_openconnect_properties_validate (NMSettingVpn *s_vpn, GError **error)
{
	ValidateInfo info = { &valid_properties[0], error, FALSE };

	nm_setting_vpn_foreach_data_item (s_vpn, validate_one_property, &info);
	if (!info.have_items) {
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             "%s",
		             _("No VPN configuration options."));
		return FALSE;
	}

	return *error ? FALSE : TRUE;
}

static gboolean
nm_openconnect_secrets_validate (NMSettingVpn *s_vpn, GError **error)
{
	ValidateInfo info = { &valid_secrets[0], error, FALSE };

	nm_setting_vpn_foreach_secret (s_vpn, validate_one_property, &info);
	if (!info.have_items) {
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             "%s",
		             _("No VPN secrets!"));
		return FALSE;
	}

	return *error ? FALSE : TRUE;
}

static char **
build_gpclient_env (NMSettingConnection *s_con)
{
	guint32 i;
	guint32 n_permissions;
	struct passwd *pw = NULL;
	char uid_buf[32];
	char gid_buf[32];
	char runtime_dir[PATH_MAX];
	char dbus_address[PATH_MAX + 32];
	char wayland_path[PATH_MAX];
	char xauthority[PATH_MAX];
	char **envp;

	if (geteuid () != 0 || !s_con)
		return NULL;

	n_permissions = nm_setting_connection_get_num_permissions (s_con);
	for (i = 0; i < n_permissions; i++) {
		const char *ptype = NULL;
		const char *pitem = NULL;

		if (!nm_setting_connection_get_permission (s_con, i, &ptype, &pitem, NULL))
			continue;
		if (ptype && !strcmp (ptype, "user") && pitem && pitem[0]) {
			pw = getpwnam (pitem);
			break;
		}
	}
	if (!pw)
		return NULL;

	g_snprintf (uid_buf, sizeof (uid_buf), "%lu", (unsigned long) pw->pw_uid);
	g_snprintf (gid_buf, sizeof (gid_buf), "%lu", (unsigned long) pw->pw_gid);

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "SUDO_UID", uid_buf, TRUE);
	envp = g_environ_setenv (envp, "SUDO_GID", gid_buf, TRUE);
	envp = g_environ_setenv (envp, "SUDO_USER", pw->pw_name, TRUE);
	envp = g_environ_setenv (envp, "HOME", pw->pw_dir, TRUE);
	envp = g_environ_setenv (envp, "USER", pw->pw_name, TRUE);
	envp = g_environ_setenv (envp, "LOGNAME", pw->pw_name, TRUE);
	envp = g_environ_setenv (envp, "BROWSER", "firefox", TRUE);
	envp = g_environ_setenv (envp, "RUST_BACKTRACE", "1", TRUE);
	envp = g_environ_setenv (envp, "RUST_LIB_BACKTRACE", "1", TRUE);

	g_snprintf (runtime_dir, sizeof (runtime_dir), "/run/user/%lu", (unsigned long) pw->pw_uid);
	g_snprintf (dbus_address, sizeof (dbus_address), "unix:path=%s/bus", runtime_dir);
	g_snprintf (wayland_path, sizeof (wayland_path), "%s/wayland-0", runtime_dir);
	g_snprintf (xauthority, sizeof (xauthority), "%s/.Xauthority", pw->pw_dir);

	envp = g_environ_setenv (envp, "XDG_RUNTIME_DIR", runtime_dir, TRUE);
	envp = g_environ_setenv (envp, "DBUS_SESSION_BUS_ADDRESS", dbus_address, TRUE);
	if (g_file_test (wayland_path, G_FILE_TEST_EXISTS)) {
		envp = g_environ_setenv (envp, "WAYLAND_DISPLAY", "wayland-0", TRUE);
		envp = g_environ_setenv (envp, "XDG_SESSION_TYPE", "wayland", TRUE);
		envp = g_environ_setenv (envp, "MOZ_ENABLE_WAYLAND", "1", TRUE);
	} else {
		envp = g_environ_setenv (envp, "DISPLAY", ":0", TRUE);
		envp = g_environ_setenv (envp, "XDG_SESSION_TYPE", "x11", TRUE);
		if (g_file_test (xauthority, G_FILE_TEST_EXISTS))
			envp = g_environ_setenv (envp, "XAUTHORITY", xauthority, TRUE);
	}

	_LOGI ("gpclient will use desktop user %s (%lu) for any helper auth",
	       pw->pw_name,
	       (unsigned long) pw->pw_uid);

	return envp;
}

static char *
create_persistent_tundev(const char *suggested_name, GError **error)
{
	struct passwd *pw;
	struct ifreq ifr;
	int fd;
	int i;

	pw = getpwnam(NM_OPENCONNECT_USER);
	if (!pw) {
		/* TODO: Is this an error case or a normal possibility
		 * where we just don't set a persistent tundev? */
		return NULL;
	}

	gl.tun_owner = pw->pw_uid;
	gl.tun_group = pw->pw_gid;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		perror("open /dev/net/tun");
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "%s",
		             _("Could not open /dev/net/tun"));
		return NULL;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

	if (suggested_name) {
		/* Not NUL-terminated if strlen(suggested_name) >= IFNAMSIZE  */
		strncpy(ifr.ifr_name, suggested_name, IFNAMSIZ);
		if (ioctl(fd, TUNSETIFF, (void *)&ifr)) {
			g_set_error (error,
			             NM_VPN_PLUGIN_ERROR,
			             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
			             _("Could not create tunnel interface named '%.*s'"),
			             IFNAMSIZ, ifr.ifr_name);
			return NULL;
		}
		} else {
			for (i = 0; i < 256; i++) {
				sprintf(ifr.ifr_name, "vpn%d", i);

				if (!ioctl(fd, TUNSETIFF, (void *)&ifr))
					break;
			}
			if (i == 256) {
			g_set_error (error,
			             NM_VPN_PLUGIN_ERROR,
			             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
			             "%s",
			             _("Could not create tunnel interface named 'vpnX', for X in 0..255"));
				return NULL;
			}
		}

	if (ioctl(fd, TUNSETOWNER, gl.tun_owner) < 0) {
		perror("TUNSETOWNER");
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             _("Could not set tunnel interface owner to '%d'"),
		             gl.tun_owner);
		return NULL;
	}

	if (ioctl(fd, TUNSETPERSIST, 1)) {
		perror("TUNSETPERSIST");
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "%s",
		             _("Could not set tunnel interface persistence"));
		return NULL;
	}
	close(fd);
	_LOGW ("Created tundev %s\n", ifr.ifr_name);
	return g_strndup(ifr.ifr_name, IFNAMSIZ);
}

static GVariant *
nm_get_property (GDBusConnection *connection,
                 const char *path,
                 const char *interface,
                 const char *property)
{
	GVariant *ret;
	GVariant *value = NULL;
	GError *error = NULL;

	ret = g_dbus_connection_call_sync (connection,
	                                   "org.freedesktop.NetworkManager",
	                                   path,
	                                   "org.freedesktop.DBus.Properties",
	                                   "Get",
	                                   g_variant_new ("(ss)", interface, property),
	                                   G_VARIANT_TYPE ("(v)"),
	                                   G_DBUS_CALL_FLAGS_NONE,
	                                   2000,
	                                   NULL,
	                                   &error);
	if (!ret) {
		_LOGD ("Could not read NetworkManager property %s.%s on %s: %s",
		       interface, property, path, error->message);
		g_clear_error (&error);
		return NULL;
	}

	{
		GVariant *wrapped = NULL;

		g_variant_get (ret, "(@v)", &wrapped);
		value = g_variant_get_variant (wrapped);
		g_variant_unref (wrapped);
	}
	g_variant_unref (ret);
	return value;
}

static gboolean
active_connection_uses_tun (GDBusConnection *connection,
                            const char *active_path,
                            const char *tun_name,
                            uid_t tun_owner)
{
	GVariant *type_v, *vpn_v, *devices_v;
	GVariantIter iter;
	const char *type;
	const char *device_path;
	gboolean vpn;
	gboolean found = FALSE;

	type_v = nm_get_property (connection,
	                          active_path,
	                          "org.freedesktop.NetworkManager.Connection.Active",
	                          "Type");
	if (!type_v)
		return FALSE;
	type = g_variant_get_string (type_v, NULL);
	if (strcmp (type, "tun") != 0) {
		g_variant_unref (type_v);
		return FALSE;
	}
	g_variant_unref (type_v);

	vpn_v = nm_get_property (connection,
	                         active_path,
	                         "org.freedesktop.NetworkManager.Connection.Active",
	                         "Vpn");
	if (vpn_v) {
		vpn = g_variant_get_boolean (vpn_v);
		g_variant_unref (vpn_v);
		if (vpn)
			return FALSE;
	}

	devices_v = nm_get_property (connection,
	                             active_path,
	                             "org.freedesktop.NetworkManager.Connection.Active",
	                             "Devices");
	if (!devices_v)
		return FALSE;

	g_variant_iter_init (&iter, devices_v);
	while (g_variant_iter_next (&iter, "&o", &device_path)) {
		GVariant *iface_v;
		GVariant *owner_v;
		const char *iface;
		gint64 owner;

		iface_v = nm_get_property (connection,
		                           device_path,
		                           "org.freedesktop.NetworkManager.Device",
		                           "Interface");
		if (!iface_v)
			continue;

		iface = g_variant_get_string (iface_v, NULL);
		if (strcmp (iface, tun_name) != 0) {
			g_variant_unref (iface_v);
			continue;
		}
		g_variant_unref (iface_v);

		owner_v = nm_get_property (connection,
		                           device_path,
		                           "org.freedesktop.NetworkManager.Device.Tun",
		                           "Owner");
		if (owner_v) {
			owner = g_variant_get_int64 (owner_v);
			g_variant_unref (owner_v);
			if (owner != (gint64) tun_owner)
				continue;
		}

		found = TRUE;
		break;
	}
	g_variant_unref (devices_v);

	return found;
}

static void
deactivate_assumed_tun_connection (const char *tun_name)
{
	GDBusConnection *connection;
	GVariant *active_v;
	GVariantIter iter;
	GError *error = NULL;
	struct passwd *pw;
	const char *active_path;

	if (!tun_name || !tun_name[0])
		return;

	pw = getpwnam (NM_OPENCONNECT_USER);
	if (!pw)
		return;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (!connection) {
		_LOGD ("Could not connect to system bus for tun cleanup: %s", error->message);
		g_clear_error (&error);
		return;
	}

	active_v = nm_get_property (connection,
	                            "/org/freedesktop/NetworkManager",
	                            "org.freedesktop.NetworkManager",
	                            "ActiveConnections");
	if (!active_v) {
		g_object_unref (connection);
		return;
	}

	g_variant_iter_init (&iter, active_v);
	while (g_variant_iter_next (&iter, "&o", &active_path)) {
		GVariant *ret;

		if (!active_connection_uses_tun (connection, active_path, tun_name, pw->pw_uid))
			continue;

		ret = g_dbus_connection_call_sync (connection,
		                                   "org.freedesktop.NetworkManager",
		                                   "/org/freedesktop/NetworkManager",
		                                   "org.freedesktop.NetworkManager",
		                                   "DeactivateConnection",
		                                   g_variant_new ("(o)", active_path),
		                                   NULL,
		                                   G_DBUS_CALL_FLAGS_NONE,
		                                   2000,
		                                   NULL,
		                                   &error);
		if (!ret) {
			_LOGW ("Could not deactivate assumed tun connection %s on %s: %s",
			       active_path, tun_name, error->message);
			g_clear_error (&error);
			continue;
		}

		_LOGI ("Deactivated assumed tun connection %s on %s", active_path, tun_name);
		g_variant_unref (ret);
	}

	g_variant_unref (active_v);
	g_object_unref (connection);
}

static void
destroy_persistent_tundev(char *tun_name)
{
	struct ifreq ifr;
	int fd;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		perror("open /dev/net/tun");
		return;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	strcpy(ifr.ifr_name, tun_name);

	if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
		perror("TUNSETIFF");
		close(fd);
		return;
	}

	if (ioctl(fd, TUNSETPERSIST, 0)) {
		perror("TUNSETPERSIST");
		close(fd);
		return;
	}
	_LOGW ("Destroyed  tundev %s\n", tun_name);
	close(fd);
}

static gboolean
read_gpclient_lock_pid (GPid *pid)
{
	char *contents = NULL;
	char *end = NULL;
	gsize len = 0;
	long parsed;

	if (!g_file_get_contents (GPCLIENT_LOCK_FILE, &contents, &len, NULL))
		return FALSE;

	errno = 0;
	parsed = strtol (contents, &end, 10);
	g_free (contents);

	if (errno || end == contents || parsed <= 0 || parsed > INT_MAX)
		return FALSE;

	*pid = (GPid) parsed;
	return TRUE;
}

static gboolean
pid_is_running (GPid pid)
{
	if (kill ((pid_t) pid, 0) == 0)
		return TRUE;
	return errno == EPERM;
}

static gboolean
pid_comm_is_gpclient (GPid pid)
{
	char path[64];
	char comm[64];
	FILE *fp;
	gboolean is_gpclient = FALSE;

	g_snprintf (path, sizeof (path), "/proc/%ld/comm", (long) pid);
	fp = fopen (path, "r");
	if (!fp)
		return TRUE;

	if (fgets (comm, sizeof (comm), fp)) {
		g_strchomp (comm);
		is_gpclient = !strcmp (comm, "gpclient");
	}
	fclose (fp);

	return is_gpclient;
}

static gboolean
prepare_gpclient_lock (GError **error)
{
	GPid pid = 0;

	if (!g_file_test (GPCLIENT_LOCK_FILE, G_FILE_TEST_EXISTS))
		return TRUE;

	if (read_gpclient_lock_pid (&pid) && pid_is_running (pid)) {
		if (pid_comm_is_gpclient (pid)) {
			g_set_error (error,
			             NM_VPN_PLUGIN_ERROR,
			             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
			             _("gpclient is already running with pid %ld"),
			             (long) pid);
			return FALSE;
		}

		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             _("Refusing to remove gpclient lock file owned by running pid %ld"),
		             (long) pid);
		return FALSE;
	}

	if (unlink (GPCLIENT_LOCK_FILE) < 0 && errno != ENOENT) {
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             _("Could not remove stale gpclient lock file: %s"),
		             g_strerror (errno));
		return FALSE;
	}

	_LOGI ("Removed stale gpclient lock file");
	return TRUE;
}

static void
gpclient_drop_child_privs(gpointer user_data)
{
	if (setpgid (0, 0) != 0)
		_LOGW ("Failed to create gpclient process group: %s", g_strerror (errno));

	/*
	 * gpclient 2.6.x assumes the main process has the same shape as
	 * `sudo -E gpclient`: root for tunnel/session management, with SUDO_UID
	 * and friends identifying the desktop user for browser auth relaunches.
	 */
}

static void
openconnect_watch_cb (GPid pid, gint status, gpointer user_data)
{
	NMOpenconnectPlugin *plugin = NM_OPENCONNECT_PLUGIN (user_data);
	NMOpenconnectPluginPrivate *priv = NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin);
	guint error = 0;
	gboolean disconnect_requested = priv->disconnect_requested;

	if (disconnect_requested) {
		if (WIFEXITED (status))
			_LOGI ("gpclient exited after requested disconnect with code %d", WEXITSTATUS (status));
		else if (WIFSIGNALED (status))
			_LOGI ("gpclient stopped after requested disconnect with signal %d", WTERMSIG (status));
		else
			_LOGI ("gpclient stopped after requested disconnect");
	} else if (WIFEXITED (status)) {
		error = WEXITSTATUS (status);
		if (error != 0)
			_LOGW ("gpclient exited with error code %d", error);
	}
	else if (WIFSTOPPED (status))
		_LOGW ("gpclient stopped unexpectedly with signal %d", WSTOPSIG (status));
	else if (WIFSIGNALED (status))
		_LOGW ("gpclient died with signal %d", WTERMSIG (status));
	else
		_LOGW ("gpclient died from an unknown cause");

	/* Reap child if needed. */
	waitpid (pid, NULL, WNOHANG);
	priv->pid = 0;
	priv->disconnect_requested = FALSE;

	if (priv->tun_name) {
		deactivate_assumed_tun_connection (priv->tun_name);
		destroy_persistent_tundev (priv->tun_name);
		g_free (priv->tun_name);
		priv->tun_name = NULL;
	}

	if (disconnect_requested)
		return;

	/* Must be after data->state is set since signals use data->state */
	switch (error) {
	case 2:
		/* Couldn't log in due to bad user/pass */
		nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (plugin), NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED);
		break;
	case 1:
		/* Other error (couldn't bind to address, etc) */
		nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (plugin), NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
		break;
	default:
		nm_vpn_service_plugin_disconnect (NM_VPN_SERVICE_PLUGIN (plugin), NULL);
		break;
	}
}

static gint
nm_openconnect_start_openconnect_binary (NMOpenconnectPlugin *plugin,
                                         NMSettingConnection *s_con,
                                         NMSettingVpn *s_vpn,
                                         GError **error)
{
	NMOpenconnectPluginPrivate *priv = NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin);
	GPid pid;
	const char **gpclient_binary = NULL;
	GPtrArray *gpclient_argv;
	GSource *gpclient_watch;
	gint stdin_fd = -1;
	gboolean use_cookie;
	gboolean direct_gateway_mode;
	char **envp = NULL;
	const char *props_portal, *props_vpn_gw, *props_cookie, *props_mtu;
	const char *props_csd_enable, *props_csd_wrapper;
	const char *props_no_dtls, *props_disable_udp, *props_auto_gateway, *props_as_gateway;
	const char *props_cert, *props_sslkey, *props_key_pass;
	const char *props_client_version, *props_reported_os, *props_fix_openssl, *props_ignore_tls, *props_browser;
	const char *props_tun_name;
	const char *connect_server;

	/* Find gpclient */
	gpclient_binary = gpclient_binary_paths;
	while (*gpclient_binary != NULL) {
		if (g_file_test (*gpclient_binary, G_FILE_TEST_EXISTS))
			break;
		gpclient_binary++;
	}

	if (!*gpclient_binary) {
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "%s",
		             _("Could not find gpclient binary."));
		return -1;
	}

	props_portal = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_PORTAL);
	props_vpn_gw = nm_setting_vpn_get_secret (s_vpn, NM_OPENCONNECT_KEY_GATEWAY);
	if (!props_vpn_gw || !strlen (props_vpn_gw))
		props_vpn_gw = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_GATEWAY);

	props_auto_gateway = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_AUTO_GATEWAY);
	props_as_gateway = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_AS_GATEWAY);
	if ((!props_vpn_gw || !strlen (props_vpn_gw)) &&
	    (!props_portal || !strlen (props_portal))) {
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "%s",
		             _("No VPN gateway specified."));
		return -1;
	}

	direct_gateway_mode = props_as_gateway && !strcmp (props_as_gateway, "yes");
	if (!props_portal || !strlen (props_portal))
		direct_gateway_mode = TRUE;

	props_cookie = nm_setting_vpn_get_secret (s_vpn, NM_OPENCONNECT_KEY_COOKIE);
	use_cookie = props_cookie && strlen (props_cookie);
	if (!use_cookie) {
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "%s",
		             _("No gpclient authentication JSON provided."));
		return -1;
	}

	props_mtu = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_MTU);
	props_cert = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_USERCERT);
	props_sslkey = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_PRIVKEY);
	props_key_pass = nm_setting_vpn_get_secret (s_vpn, NM_OPENCONNECT_KEY_KEY_PASS);
	if (!props_key_pass || !strlen (props_key_pass))
		props_key_pass = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_KEY_PASS);
	props_client_version = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_CLIENT_VERSION);
	props_reported_os = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_REPORTED_OS);
	props_fix_openssl = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_FIX_OPENSSL);
	props_ignore_tls = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_IGNORE_TLS_ERRORS);
	props_browser = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_BROWSER);
	envp = build_gpclient_env (s_con);
	envp = g_environ_setenv (envp, "NM_GPCLIENT_BROWSER",
	                        props_browser && strlen (props_browser) ? props_browser : "default",
	                        TRUE);

	if (direct_gateway_mode)
		connect_server = (props_vpn_gw && strlen (props_vpn_gw)) ? props_vpn_gw : props_portal;
	else
		connect_server = (props_portal && strlen (props_portal)) ? props_portal : props_vpn_gw;

	gpclient_argv = g_ptr_array_new ();
	g_ptr_array_add (gpclient_argv, (gpointer) (*gpclient_binary));
	if (props_fix_openssl && !strcmp (props_fix_openssl, "yes"))
		g_ptr_array_add (gpclient_argv, (gpointer) "--fix-openssl");
	if (props_ignore_tls && !strcmp (props_ignore_tls, "yes"))
		g_ptr_array_add (gpclient_argv, (gpointer) "--ignore-tls-errors");
	g_ptr_array_add (gpclient_argv, (gpointer) "connect");
	g_ptr_array_add (gpclient_argv, (gpointer) connect_server);
	if (use_cookie)
		g_ptr_array_add (gpclient_argv, (gpointer) "--cookie-on-stdin");
	g_ptr_array_add (gpclient_argv, (gpointer) "--browser");
	g_ptr_array_add (gpclient_argv, (gpointer) NM_GPCLIENT_BROWSER_HELPER_PATH);

	if (direct_gateway_mode) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--as-gateway");
	} else if (props_portal && strlen (props_portal)) {
		if (props_auto_gateway && !strcmp (props_auto_gateway, "yes")) {
			if (gpclient_supports_connect_arg (*gpclient_binary, "--auto-gateway")) {
				g_ptr_array_add (gpclient_argv, (gpointer) "--auto-gateway");
			} else {
				_LOGI ("gpclient does not support --auto-gateway; using portal-only connect");
			}
		} else if (props_vpn_gw && strlen (props_vpn_gw)) {
			g_ptr_array_add (gpclient_argv, (gpointer) "--gateway");
			g_ptr_array_add (gpclient_argv, (gpointer) props_vpn_gw);
		}
	}

	if (props_mtu && strlen(props_mtu)) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--mtu");
		g_ptr_array_add (gpclient_argv, (gpointer) props_mtu);
	}

	if (props_cert && strlen(props_cert)) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--certificate");
		g_ptr_array_add (gpclient_argv, (gpointer) props_cert);
	}

	if (props_sslkey && strlen(props_sslkey)) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--sslkey");
		g_ptr_array_add (gpclient_argv, (gpointer) props_sslkey);
	}

	if (props_key_pass && strlen(props_key_pass)) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--key-password");
		g_ptr_array_add (gpclient_argv, (gpointer) props_key_pass);
	}

	if (props_client_version && strlen(props_client_version)) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--client-version");
		g_ptr_array_add (gpclient_argv, (gpointer) props_client_version);
	}

	if (props_reported_os && strlen(props_reported_os)) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--os");
		g_ptr_array_add (gpclient_argv, (gpointer) props_reported_os);
	}

	g_ptr_array_add (gpclient_argv, (gpointer) "--script");
	g_ptr_array_add (gpclient_argv, (gpointer) NM_GPCLIENT_HELPER_PATH);

	props_tun_name = nm_setting_connection_get_interface_name(s_con);
	priv->tun_name = create_persistent_tundev (props_tun_name, error);
	if (!priv->tun_name && *error)
		return -1;
	else if (priv->tun_name) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--interface");
		g_ptr_array_add (gpclient_argv, (gpointer) priv->tun_name);
	} else {
		/* TODO: Is this a valid case? See create_persistent_tundev. */
	}

	props_no_dtls = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_NO_DTLS);
	props_disable_udp = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_DISABLE_UDP);
	if ((props_no_dtls && !strcmp (props_no_dtls, "yes")) ||
	    (props_disable_udp && !strcmp (props_disable_udp, "yes"))) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--no-dtls");
	}

	props_csd_enable = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_CSD_ENABLE);
	props_csd_wrapper = nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_CSD_WRAPPER);
	if (props_csd_enable && !strcmp (props_csd_enable, "yes") && props_csd_wrapper) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--hip");
		if (strlen (props_csd_wrapper)) {
			g_ptr_array_add (gpclient_argv, (gpointer) props_csd_wrapper);
		}
	}

	if (gl.log_level >= LOG_INFO) {
		g_ptr_array_add (gpclient_argv, (gpointer) "--verbose");
		if (gl.log_level >= LOG_DEBUG)
			g_ptr_array_add (gpclient_argv, (gpointer) "--verbose");
	}

	g_ptr_array_add (gpclient_argv, NULL);

	if (!prepare_gpclient_lock (error)) {
		g_ptr_array_free (gpclient_argv, TRUE);
		g_strfreev (envp);
		if (priv->tun_name) {
			destroy_persistent_tundev (priv->tun_name);
			g_free (priv->tun_name);
			priv->tun_name = NULL;
		}
		return -1;
	}

	if (!g_spawn_async_with_pipes (NULL, (char **) gpclient_argv->pdata, envp,
	                               G_SPAWN_DO_NOT_REAP_CHILD,
	                               gpclient_drop_child_privs, priv->tun_name,
	                               &pid, use_cookie ? &stdin_fd : NULL, NULL, NULL, error)) {
		g_ptr_array_free (gpclient_argv, TRUE);
		g_strfreev (envp);
		_LOGW ("gpclient failed to start.  error: '%s'", (*error)->message);
		return -1;
	}
	g_ptr_array_free (gpclient_argv, TRUE);
	g_strfreev (envp);

	_LOGI ("gpclient started with pid %d", pid);

	if (use_cookie) {
		if (write(stdin_fd, props_cookie, strlen(props_cookie)) != strlen(props_cookie) ||
		    write(stdin_fd, "\n", 1) != 1) {
			_LOGW ("gpclient didn't read the authentication JSON we fed it");
			return -1;
		}

		close(stdin_fd);
	}

	NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = pid;
	NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->disconnect_requested = FALSE;
	gpclient_watch = g_child_watch_source_new (pid);
	g_source_set_callback (gpclient_watch, (GSourceFunc) openconnect_watch_cb, plugin, NULL);
	g_source_attach (gpclient_watch, NULL);
	g_source_unref (gpclient_watch);

	return 0;
}
static gboolean
real_connect (NMVpnServicePlugin   *plugin,
              NMConnection  *connection,
              GError       **error)
{
	NMSettingConnection *s_con;
	NMSettingVpn *s_vpn;
	gint openconnect_fd = -1;

	s_con = nm_connection_get_setting_connection(connection);
	g_assert (s_con);
	if (!s_con)
		goto out;
	s_vpn = nm_connection_get_setting_vpn (connection);
	g_assert (s_vpn);
	if (!nm_openconnect_properties_validate (s_vpn, error))
		goto out;
	if (!nm_openconnect_secrets_validate (s_vpn, error))
		goto out;

	if (_LOGD_enabled ())
		nm_connection_dump (connection);

	openconnect_fd = nm_openconnect_start_openconnect_binary (NM_OPENCONNECT_PLUGIN (plugin), s_con, s_vpn, error);
	if (!openconnect_fd)
		return TRUE;

 out:
	return FALSE;
}

static gboolean
real_need_secrets (NMVpnServicePlugin *plugin,
                   NMConnection *connection,
                   const char **setting_name,
                   GError **error)
{
	NMOpenconnectPluginPrivate *priv = NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin);
	NMSettingConnection *s_con;
	NMSettingVpn *s_vpn;
	const char *tun_name;

	g_return_val_if_fail (NM_IS_VPN_SERVICE_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	s_con = nm_connection_get_setting_connection (connection);
	if (!priv->pid) {
		tun_name = s_con ? nm_setting_connection_get_interface_name (s_con) : NULL;
		deactivate_assumed_tun_connection (tun_name && tun_name[0] ? tun_name : "vpn0");
	}

	s_vpn = nm_connection_get_setting_vpn (connection);
	if (!s_vpn) {
		g_set_error (error,
		             NM_VPN_PLUGIN_ERROR,
		             NM_VPN_PLUGIN_ERROR_INVALID_CONNECTION,
		             "%s",
		             "Could not process the request because the VPN connection settings were invalid.");
		return FALSE;
	}

	if (!nm_setting_vpn_get_secret (s_vpn, NM_OPENCONNECT_KEY_GATEWAY) &&
	    !nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_GATEWAY) &&
	    !nm_setting_vpn_get_data_item (s_vpn, NM_OPENCONNECT_KEY_PORTAL)) {
		*setting_name = NM_SETTING_VPN_SETTING_NAME;
		return TRUE;
	}

	/* Run browser authentication through NetworkManager's user-session
	 * auth dialog for both portal and direct-gateway modes, then feed
	 * the returned gpclient JSON to gpclient via --cookie-on-stdin. */
	if (!nm_setting_vpn_get_secret (s_vpn, NM_OPENCONNECT_KEY_COOKIE)) {
		*setting_name = NM_SETTING_VPN_SETTING_NAME;
		return TRUE;
	}

	return FALSE;
}

static gboolean
ensure_killed (gpointer data)
{
	int pid = GPOINTER_TO_INT (data);

	if (kill (-pid, 0) == 0)
		kill (-pid, SIGKILL);
	else if (kill (pid, 0) == 0)
		kill (pid, SIGKILL);

	return FALSE;
}

static gboolean
real_disconnect (NMVpnServicePlugin   *plugin,
                 GError       **err)
{
	NMOpenconnectPluginPrivate *priv = NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin);

	if (priv->pid) {
		priv->disconnect_requested = TRUE;
		if (kill (-priv->pid, SIGINT) == 0)
			g_timeout_add (2000, ensure_killed, GINT_TO_POINTER (priv->pid));
		else if (kill (priv->pid, SIGINT) == 0)
			g_timeout_add (2000, ensure_killed, GINT_TO_POINTER (priv->pid));
		else
			kill (-priv->pid, SIGKILL);

		_LOGI ("Terminated gpclient daemon with PID %d.", priv->pid);
	}

	return TRUE;
}

static void
nm_openconnect_plugin_init (NMOpenconnectPlugin *plugin)
{
}

static void
nm_openconnect_plugin_class_init (NMOpenconnectPluginClass *openconnect_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (openconnect_class);
	NMVpnServicePluginClass *parent_class = NM_VPN_SERVICE_PLUGIN_CLASS (openconnect_class);

	g_type_class_add_private (object_class, sizeof (NMOpenconnectPluginPrivate));

	/* virtual methods */
	parent_class->connect    = real_connect;
	parent_class->need_secrets = real_need_secrets;
	parent_class->disconnect = real_disconnect;
}

NMOpenconnectPlugin *
nm_openconnect_plugin_new (const char *bus_name)
{
	NMOpenconnectPlugin *plugin;
	GError *error = NULL;

	plugin = (NMOpenconnectPlugin *) g_initable_new (NM_TYPE_OPENCONNECT_PLUGIN, NULL, &error,
	                                                 NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name,
	                                                 NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, !gl.debug,
	                                                 NULL);
	if (!plugin) {
		_LOGW ("Failed to initialize a plugin instance: %s", error->message);
		g_error_free (error);
	}

	return plugin;
}

static void
signal_handler (int signo)
{
	if (signo == SIGINT || signo == SIGTERM)
		g_main_loop_quit (gl.loop);
}

static void
setup_signals (void)
{
	struct sigaction action;
	sigset_t mask;

	sigemptyset (&mask);
	action.sa_handler = signal_handler;
	action.sa_mask = mask;
	action.sa_flags = 0;
	sigaction (SIGTERM,  &action, NULL);
	sigaction (SIGINT,  &action, NULL);
}

static void
quit_mainloop (NMOpenconnectPlugin *plugin, gpointer user_data)
{
	g_main_loop_quit ((GMainLoop *) user_data);
}

int main (int argc, char *argv[])
{
	NMOpenconnectPlugin *plugin;
	gboolean persist = FALSE;
	GOptionContext *opt_ctx = NULL;
	gchar *bus_name = NM_DBUS_SERVICE_OPENCONNECT;
	char sbuf[30];

	GOptionEntry options[] = {
		{ "persist", 0, 0, G_OPTION_ARG_NONE, &persist, N_("Don’t quit when VPN connection terminates"), NULL },
		{ "debug", 0, 0, G_OPTION_ARG_NONE, &gl.debug, N_("Enable verbose debug logging (may expose passwords)"), NULL },
		{ "bus-name", 0, 0, G_OPTION_ARG_STRING, &bus_name, N_("D-Bus name to use for this instance"), NULL },
		{NULL}
	};

#if !GLIB_CHECK_VERSION (2, 35, 0)
	g_type_init ();
#endif

	/* locale will be set according to environment LC_* variables */
	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, NM_OPENCONNECT_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* Parse options */
	opt_ctx = g_option_context_new (NULL);
	g_option_context_set_translation_domain (opt_ctx, GETTEXT_PACKAGE);
	g_option_context_set_ignore_unknown_options (opt_ctx, FALSE);
	g_option_context_set_help_enabled (opt_ctx, TRUE);
	g_option_context_add_main_entries (opt_ctx, options, NULL);

	g_option_context_set_summary (opt_ctx,
	                              _("nm-gpclient-service provides integrated "
	                                "GlobalProtect gpclient VPN capability to NetworkManager."));

	g_option_context_parse (opt_ctx, &argc, &argv, NULL);
	g_option_context_free (opt_ctx);

	if (getenv ("OPENCONNECT_DEBUG"))
		gl.debug = TRUE;

	gl.log_level = _nm_utils_ascii_str_to_int64 (getenv ("NM_VPN_LOG_LEVEL"),
	                                             10, 0, LOG_DEBUG,
	                                             gl.debug ? LOG_INFO : LOG_NOTICE);

	/* set logging options for helper script. */
	setenv ("NM_VPN_LOG_LEVEL", nm_sprintf_buf (sbuf, "%d", gl.log_level), TRUE);
	setenv ("NM_VPN_LOG_PREFIX_TOKEN", nm_sprintf_buf (sbuf, "%ld", (long) getpid ()), TRUE);

	_LOGD ("nm-gpclient-service (version " DIST_VERSION ") starting...");

	if (system ("/sbin/modprobe tun") == -1)
		exit (EXIT_FAILURE);

	if (bus_name)
		setenv ("NM_DBUS_SERVICE_OPENCONNECT", bus_name, 0);

	plugin = nm_openconnect_plugin_new (bus_name);
	if (!plugin)
		exit (EXIT_FAILURE);

	gl.loop = g_main_loop_new (NULL, FALSE);

	if (!persist)
		g_signal_connect (plugin, "quit", G_CALLBACK (quit_mainloop), gl.loop);

	setup_signals ();
	g_main_loop_run (gl.loop);

	g_clear_pointer (&gl.loop, g_main_loop_unref);
	g_object_unref (plugin);

	exit (EXIT_SUCCESS);
}
