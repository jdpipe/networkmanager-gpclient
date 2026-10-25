/* Exercise the production callbacks without a system bus or VPN process. */
int gpclient_service_main (int argc, char *argv[]);
#define main gpclient_service_main
#include "../src/nm-gpclient-service.c"
#undef main

static NMConnection *
new_profile (void)
{
	NMConnection *connection = nm_simple_connection_new ();
	NMSetting *s_con = nm_setting_connection_new ();
	NMSettingVpn *vpn = NM_SETTING_VPN (nm_setting_vpn_new ());

	g_object_set (s_con,
	              NM_SETTING_CONNECTION_ID, "secret-flags-test",
	              NM_SETTING_CONNECTION_UUID, "a4384907-c5b0-4b36-a397-d74c94851a15",
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME, NULL);
	nm_connection_add_setting (connection, s_con);
	g_object_set (vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_VPN_SERVICE_TYPE_OPENCONNECT, NULL);
	nm_setting_vpn_add_data_item (vpn, "gateway", "gateway.example.edu");
	nm_setting_vpn_add_data_item (vpn, "as_gateway", "yes");
	nm_setting_vpn_add_data_item (vpn, "cookie-flags", "2");
	nm_setting_vpn_add_data_item (vpn, "gateway-flags", "2");
	nm_connection_add_setting (connection, NM_SETTING (vpn));
	return connection;
}

static void
test_unsafe_flags (void)
{
	const char *keys[] = { "cookie", "gateway" };
	const char *values[] = { NULL, "", "0", "1", "4", "5", "6", "7", "8", "-1", "invalid", "2junk" };
	guint i, j, with_cookie;

	for (i = 0; i < G_N_ELEMENTS (keys); i++) {
		for (j = 0; j < G_N_ELEMENTS (values); j++) {
			for (with_cookie = 0; with_cookie < 2; with_cookie++) {
				NMConnection *connection = new_profile ();
				NMSettingVpn *vpn = nm_connection_get_setting_vpn (connection);
				NMVpnServicePlugin *plugin = g_object_new (NM_TYPE_OPENCONNECT_PLUGIN, NULL);
				char *key = g_strdup_printf ("%s-flags", keys[i]);
				const char *setting = NULL;
				GError *error = NULL;

				if (values[j])
					nm_setting_vpn_add_data_item (vpn, key, values[j]);
				else
					nm_setting_vpn_remove_data_item (vpn, key);
				if (with_cookie) {
					nm_setting_vpn_add_secret (vpn, "cookie", "expired-test-token");
					nm_setting_vpn_add_secret (vpn, "gateway", "gateway.example.edu");
				}

				/* Suppress unrelated TUN cleanup even if validation regresses. */
				NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 1;
				g_assert (!real_need_secrets (plugin, connection, &setting, &error));
				NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 0;
				g_assert_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_INVALID_CONNECTION);
				g_assert (strstr (error->message, keys[i]) != NULL);
				g_assert (strstr (error->message, "cookie-flags=2") != NULL);
				g_assert (strstr (error->message, "gateway-flags=2") != NULL);
				g_assert (strstr (error->message, "expired-test-token") == NULL);
				g_assert (setting == NULL);
				g_clear_error (&error);

				/* A second validation error is a safety stop: a missing guard
				 * must fail the assertion without launching the real gpclient. */
				nm_setting_vpn_add_data_item (vpn, "no_dtls", "invalid-test-value");
				g_assert (!real_connect (plugin, connection, &error));
				g_assert_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_INVALID_CONNECTION);
				g_clear_error (&error);
				g_assert_cmpint (NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid, ==, 0);

				g_free (key);
				g_object_unref (plugin);
				g_object_unref (connection);
			}
		}
	}
}

static void
test_safe_flags (void)
{
	const char *flags[] = { "2", "3" };
	const char *cookies[] = { NULL, "", "fresh-test-token" };
	guint i, j, k;

	for (i = 0; i < G_N_ELEMENTS (flags); i++) {
		for (j = 0; j < G_N_ELEMENTS (flags); j++) {
			for (k = 0; k < G_N_ELEMENTS (cookies); k++) {
				NMConnection *connection = new_profile ();
				NMSettingVpn *vpn = nm_connection_get_setting_vpn (connection);
				NMVpnServicePlugin *plugin = g_object_new (NM_TYPE_OPENCONNECT_PLUGIN, NULL);
				const char *setting = NULL;
				GError *error = NULL;
				gboolean expected = !cookies[k] || !cookies[k][0];

				nm_setting_vpn_add_data_item (vpn, "cookie-flags", flags[i]);
				nm_setting_vpn_add_data_item (vpn, "gateway-flags", flags[j]);
				if (cookies[k])
					nm_setting_vpn_add_secret (vpn, "cookie", cookies[k]);
				NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 1;
				g_assert_cmpint (real_need_secrets (plugin, connection, &setting, &error), ==, expected);
				NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin)->pid = 0;
				g_assert_no_error (error);
				g_assert_cmpstr (setting, ==, expected ? NM_SETTING_VPN_SETTING_NAME : NULL);
				g_assert_cmpstr (nm_setting_vpn_get_secret (vpn, "cookie"), ==, cookies[k]);

				/* Safe flags get past the guard into ordinary validation. */
				nm_setting_vpn_add_data_item (vpn, "no_dtls", "invalid-test-value");
				g_assert (!real_connect (plugin, connection, &error));
				g_assert_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS);
				g_clear_error (&error);
				g_object_unref (plugin);
				g_object_unref (connection);
			}
		}
	}
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/secrets/reject-unsafe-flags", test_unsafe_flags);
	g_test_add_func ("/secrets/accept-not-saved", test_safe_flags);
	return g_test_run ();
}
