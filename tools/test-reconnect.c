/* Exercise the production NeedSecrets callback without exporting a D-Bus service. */
int gpclient_service_main (int argc, char *argv[]);
#define main gpclient_service_main
#include "../src/nm-gpclient-service.c"
#undef main

#if NM_CHECK_VERSION(1, 32, 0)

static NMConnection *
round_trip (NMConnection *connection, NMConnectionSerializationFlags flags)
{
	GVariant *settings = nm_connection_to_dbus (connection, flags);
	GError *error = NULL;
	NMConnection *copy = nm_simple_connection_new_from_dbus (settings, &error);

	g_assert_no_error (error);
	g_assert ((copy) != NULL);
	g_variant_unref (settings);
	return copy;
}

static void
assert_need_secrets (NMVpnServicePlugin *plugin, NMConnection *connection, gboolean expected)
{
	const char *setting = NULL;
	GError *error = NULL;

	/* Skip unrelated system-bus/TUN cleanup in this unprivileged test. No
	 * process is launched; reset pid before calling Disconnect below. */
	NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 1;
	g_assert_cmpint (real_need_secrets (plugin, connection, &setting, &error), ==, expected);
	NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 0;
	g_assert_no_error (error);
	if (expected)
		g_assert_cmpstr (setting, ==, NM_SETTING_VPN_SETTING_NAME);
}

static void
authenticate (NMConnection *connection, const char *dialog, const char *cookie)
{
	NMSettingVpn *vpn = nm_connection_get_setting_vpn (connection);
	GString *input = g_string_new (NULL);
	const char *keys[] = { "portal", "gateway", "as_gateway", "browser", NULL };
	char *argv[] = { (char *) dialog, "--uuid", "test-uuid", "--name", "reconnect-test",
	                 "--service", NM_VPN_SERVICE_TYPE_OPENCONNECT, "--allow-interaction", NULL };
	GError *error = NULL;
	GPid pid;
	int in_fd, out_fd, status;
	FILE *output;
	char key[64], value[512];
	guint i, count = 0;

	for (i = 0; keys[i]; i++) {
		const char *data = nm_setting_vpn_get_data_item (vpn, keys[i]);
		if (data)
			g_string_append_printf (input, "DATA_KEY=%s\nDATA_VAL=%s\n", keys[i], data);
	}
	g_string_append (input, "DONE\n");
	g_setenv ("GPAUTH_COOKIE", cookie, TRUE);
	g_assert (g_spawn_async_with_pipes (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
	                                       NULL, NULL, &pid, &in_fd, &out_fd, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (write (in_fd, input->str, input->len), ==, input->len);
	g_string_free (input, TRUE);
	output = fdopen (out_fd, "r");
	g_assert ((output) != NULL);
	while (fgets (key, sizeof (key), output) && strcmp (key, "\n")) {
		g_assert ((fgets (value, sizeof (value), output)) != NULL);
		g_strchomp (key);
		g_strchomp (value);
		nm_setting_vpn_add_secret (vpn, key, value);
		count++;
	}
	fclose (output);
	g_assert_cmpint (write (in_fd, "QUIT\n", 5), ==, 5);
	close (in_fd);
	g_assert_cmpint (waitpid (pid, &status, 0), ==, pid);
	g_spawn_close_pid (pid);
	g_assert (WIFEXITED (status));
	g_assert_cmpint (WEXITSTATUS (status), ==, 0);
	g_assert_cmpuint (count, ==, 2);
}

static void
test_reconnect (const char *data, const char *dialog, gboolean direct)
{
	NMConnection *profile = nm_simple_connection_new ();
	NMSetting *s_con = nm_setting_connection_new ();
	NMSettingVpn *vpn = NM_SETTING_VPN (nm_setting_vpn_new ());
	NMVpnServicePlugin *plugin = g_object_new (NM_TYPE_OPENCONNECT_PLUGIN, NULL);
	NMConnection *saved, *reconnect, *handoff, *unsafe;
	char **items = g_strsplit (data, ",", -1);
	char *expired_cookie;
	guint i;
	const char *gateway = direct ? "gateway.example.edu" : "vpn.example.edu";

	g_object_set (s_con, NM_SETTING_CONNECTION_ID, "reconnect-test",
	              NM_SETTING_CONNECTION_UUID, "a4384907-c5b0-4b36-a397-d74c94851a15",
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME, NULL);
	nm_connection_add_setting (profile, s_con);
	g_object_set (vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_VPN_SERVICE_TYPE_OPENCONNECT, NULL);
	nm_connection_add_setting (profile, NM_SETTING (vpn));
	for (i = 0; items[i]; i++) {
		char **pair = g_strsplit (items[i], "=", 2);
		g_assert ((pair[1]) != NULL);
		nm_setting_vpn_add_data_item (vpn, pair[0], pair[1]);
		g_strfreev (pair);
	}
	g_strfreev (items);
	if (direct) {
		nm_setting_vpn_remove_data_item (vpn, "portal");
		nm_setting_vpn_remove_data_item (vpn, "auto_gateway");
		nm_setting_vpn_add_data_item (vpn, "gateway", gateway);
		nm_setting_vpn_add_data_item (vpn, "as_gateway", "yes");
	}

	assert_need_secrets (plugin, profile, TRUE);
	authenticate (profile, dialog, "first-token");
	assert_need_secrets (plugin, profile, FALSE);
	expired_cookie = g_strdup (nm_setting_vpn_get_secret (vpn, "cookie"));
	g_assert ((strstr (expired_cookie, "first-token")) != NULL);
	g_assert_cmpstr (nm_setting_vpn_get_secret (vpn, "gateway"), ==, gateway);

	/* Omitting either flag retains that secret in libnm's system-owned
	 * serialisation. The service must reject this unsafe configuration. */
	for (i = 0; i < 2; i++) {
		const char *key = i ? "gateway" : "cookie";
		NMSettingSecretFlags flags;
		NMConnection *unflagged = nm_simple_connection_new_clone (profile);
		NMSettingVpn *unflagged_vpn = nm_connection_get_setting_vpn (unflagged);
		char *flag_key = g_strdup_printf ("%s-flags", key);
		g_assert (nm_setting_get_secret_flags (NM_SETTING (vpn), key, &flags, NULL));
		g_assert_cmpint (flags, ==, NM_SETTING_SECRET_FLAG_NOT_SAVED);
		nm_setting_vpn_remove_data_item (unflagged_vpn, flag_key);
		unsafe = round_trip (unflagged, NM_CONNECTION_SERIALIZE_WITH_NON_SECRET |
		                                NM_CONNECTION_SERIALIZE_WITH_SECRETS_SYSTEM_OWNED);
		g_assert ((nm_setting_vpn_get_secret (nm_connection_get_setting_vpn (unsafe), key)) != NULL);
		/* Unsafe profiles must now fail before a retained token is accepted. */
		{
			const char *setting = NULL;
			GError *error = NULL;

			NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 1;
			g_assert (!real_need_secrets (plugin, unsafe, &setting, &error));
			NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 0;
			g_assert_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_INVALID_CONNECTION);
			g_clear_error (&error);
		}
		g_object_unref (unsafe);
		g_object_unref (unflagged);
		g_free (flag_key);
	}

	/* Model the NM persistence boundary using libnm, not a mock flag filter.
	 * NM clears activation secrets once connected; they must not return from
	 * saved settings on the next activation. Treat the first token as expired;
	 * actual expiry and gateway rejection require an installed end-to-end test. */
	saved = round_trip (profile, NM_CONNECTION_SERIALIZE_WITH_NON_SECRET |
	                            NM_CONNECTION_SERIALIZE_WITH_SECRETS_SYSTEM_OWNED);
	nm_connection_clear_secrets (profile);
	g_assert (real_disconnect (plugin, NULL));
	reconnect = nm_simple_connection_new_clone (saved);
	g_assert ((nm_setting_vpn_get_secret (nm_connection_get_setting_vpn (reconnect), "cookie")) == NULL);
	g_assert ((nm_setting_vpn_get_secret (nm_connection_get_setting_vpn (reconnect), "gateway")) == NULL);
	assert_need_secrets (plugin, reconnect, TRUE);
	authenticate (reconnect, dialog, "fresh-token");
	assert_need_secrets (plugin, reconnect, FALSE);

	/* Connect receives current secrets over D-Bus even though they are NOT_SAVED. */
	handoff = round_trip (reconnect, NM_CONNECTION_SERIALIZE_ALL);
	vpn = nm_connection_get_setting_vpn (handoff);
	g_assert_cmpstr (nm_setting_vpn_get_secret (vpn, "cookie"), !=, expired_cookie);
	g_assert ((strstr (nm_setting_vpn_get_secret (vpn, "cookie"), "fresh-token")) != NULL);
	g_assert_cmpstr (nm_setting_vpn_get_secret (vpn, "gateway"), ==, gateway);
	if (direct)
		g_assert_cmpstr (nm_setting_vpn_get_data_item (vpn, "gateway"), ==, gateway);
	g_free (expired_cookie);
	g_object_unref (handoff);
	g_object_unref (reconnect);
	g_object_unref (saved);
	g_object_unref (profile);
	g_object_unref (plugin);
}

int
main (int argc, char **argv)
{
	g_assert_cmpint (argc, ==, 3);
	alarm (20);
	test_reconnect (argv[1], argv[2], FALSE);
	test_reconnect (argv[1], argv[2], TRUE);
	return 0;
}

#else
int
main (void)
{
	g_printerr ("reconnect regression requires libnm >= 1.32 for secret filtering\n");
	return 77;
}
#endif
