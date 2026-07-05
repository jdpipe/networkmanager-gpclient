/*
 * Minimal NetworkManager auth dialog for gpclient.
 *
 * This runs in the user's session, invokes gpauth for browser-based
 * GlobalProtect authentication, and returns the resulting JSON as the VPN
 * "cookie" secret consumed by nm-gpclient-service.
 */

#include "nm-default.h"

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <NetworkManager.h>

static const struct option long_options[] = {
	{ "reprompt", no_argument, NULL, 'r' },
	{ "uuid", required_argument, NULL, 'u' },
	{ "name", required_argument, NULL, 'n' },
	{ "service", required_argument, NULL, 's' },
	{ "allow-interaction", no_argument, NULL, 'i' },
	{ NULL, 0, NULL, 0 }
};

static const char *
lookup_nonempty(GHashTable *hash, const char *key)
{
	const char *value;

	if (!hash)
		return NULL;

	value = g_hash_table_lookup(hash, key);
	return value && value[0] ? value : NULL;
}

static gboolean
is_yes(const char *value)
{
	return value && !strcmp(value, "yes");
}

static const char *
lookup_secret_or_option(GHashTable *options, GHashTable *secrets, const char *key)
{
	const char *value;

	value = lookup_nonempty(secrets, key);
	return value ? value : lookup_nonempty(options, key);
}

static void
add_optional_arg(GPtrArray *argv, const char *arg, const char *value)
{
	if (!value)
		return;

	g_ptr_array_add(argv, (gpointer) arg);
	g_ptr_array_add(argv, (gpointer) value);
}

static gboolean
gpauth_supports_arg(const char *arg)
{
	static char *help_text = NULL;
	static gboolean help_loaded = FALSE;
	GError *error = NULL;
	gint exit_status = 0;

	if (!help_loaded) {
		if (!g_spawn_command_line_sync("gpauth --help",
		                               &help_text,
		                               NULL,
		                               &exit_status,
		                               &error)) {
			fprintf(stderr, "Failed to inspect gpauth options: %s\n", error->message);
			g_clear_error(&error);
		}
		help_loaded = TRUE;
	}

	return help_text && g_strstr_len(help_text, -1, arg);
}

static void
add_optional_gpauth_arg(GPtrArray *argv, const char *arg, const char *value)
{
	if (!value)
		return;

	if (!gpauth_supports_arg(arg)) {
		fprintf(stderr, "Skipping unsupported gpauth option '%s'\n", arg);
		return;
	}

	add_optional_arg(argv, arg, value);
}

static void
add_optional_gpauth_setting_arg(GPtrArray *argv, GHashTable *options, const char *key, const char *arg)
{
	add_optional_gpauth_arg(argv, arg, lookup_nonempty(options, key));
}

static void
add_optional_gpauth_secret_arg(GPtrArray *argv,
                               GHashTable *options,
                               GHashTable *secrets,
                               const char *key,
                               const char *arg)
{
	add_optional_gpauth_arg(argv, arg, lookup_secret_or_option(options, secrets, key));
}

static void
add_gpauth_boolean_arg(GPtrArray *argv, GHashTable *options, const char *key, const char *arg)
{
	if (!is_yes(lookup_nonempty(options, key)))
		return;

	if (!gpauth_supports_arg(arg)) {
		fprintf(stderr, "Skipping unsupported gpauth option '%s'\n", arg);
		return;
	}

	g_ptr_array_add(argv, (gpointer) arg);
}

static char *
strip_one_trailing_newline(char *value)
{
	gsize len;

	if (!value)
		return NULL;

	len = strlen(value);
	while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r'))
		value[--len] = '\0';

	return value;
}

static void
wait_for_quit_or_eof(void)
{
	struct pollfd pfd;
	char line[16];
	gsize len = 0;

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN | POLLHUP;
	pfd.revents = 0;

	while (poll(&pfd, 1, 30000) > 0) {
		char c;
		ssize_t n;

		n = read(STDIN_FILENO, &c, 1);
		if (n <= 0)
			return;

		if (c == '\0' || c == '\n') {
			line[len] = '\0';
			if (!strcmp(line, "QUIT"))
				return;
			len = 0;
			continue;
		}

		if (len + 1 < sizeof(line))
			line[len++] = c;
	}
}

static gboolean
run_gpauth(const char *server,
           gboolean as_gateway,
           const char *browser,
           GHashTable *options,
           GHashTable *secrets,
           char **auth_json)
{
	GPtrArray *argv;
	GError *error = NULL;
	char *stdout_buf = NULL;
	char *stderr_buf = NULL;
	gint exit_status = 0;
	gboolean ok;

	argv = g_ptr_array_new();
	g_ptr_array_add(argv, "gpauth");
	if (as_gateway)
		g_ptr_array_add(argv, "--gateway");
	g_ptr_array_add(argv, (gpointer) server);
	g_ptr_array_add(argv, "--browser");
	g_ptr_array_add(argv, (gpointer) browser);
	add_optional_gpauth_setting_arg(argv, options, NM_OPENCONNECT_KEY_USERCERT, "--certificate");
	add_optional_gpauth_setting_arg(argv, options, NM_OPENCONNECT_KEY_PRIVKEY, "--sslkey");
	add_optional_gpauth_secret_arg(argv, options, secrets, NM_OPENCONNECT_KEY_KEY_PASS, "--key-password");
	add_optional_gpauth_setting_arg(argv, options, NM_OPENCONNECT_KEY_CLIENT_VERSION, "--client-version");
	add_optional_gpauth_setting_arg(argv, options, NM_OPENCONNECT_KEY_HOST_ID, "--host-id");
	add_optional_gpauth_setting_arg(argv, options, NM_OPENCONNECT_KEY_REPORTED_OS, "--os");
	add_gpauth_boolean_arg(argv, options, NM_OPENCONNECT_KEY_FIX_OPENSSL, "--fix-openssl");
	add_gpauth_boolean_arg(argv, options, NM_OPENCONNECT_KEY_IGNORE_TLS_ERRORS, "--ignore-tls-errors");
	g_ptr_array_add(argv, NULL);

	ok = g_spawn_sync(NULL,
	                  (char **) argv->pdata,
	                  NULL,
	                  G_SPAWN_SEARCH_PATH,
	                  NULL,
	                  NULL,
	                  &stdout_buf,
	                  &stderr_buf,
	                  &exit_status,
	                  &error);
	g_ptr_array_free(argv, TRUE);

	if (!ok) {
		fprintf(stderr, "Failed to start gpauth: %s\n", error->message);
		g_clear_error(&error);
		g_free(stdout_buf);
		g_free(stderr_buf);
		return FALSE;
	}

	if (!WIFEXITED(exit_status) || WEXITSTATUS(exit_status) != 0) {
		if (WIFEXITED(exit_status))
			fprintf(stderr, "gpauth exited with status %d\n", WEXITSTATUS(exit_status));
		else if (WIFSIGNALED(exit_status))
			fprintf(stderr, "gpauth died with signal %d\n", WTERMSIG(exit_status));
		else
			fprintf(stderr, "gpauth failed with wait status %d\n", exit_status);
		if (stderr_buf && stderr_buf[0])
			fprintf(stderr, "%s\n", stderr_buf);
		g_free(stdout_buf);
		g_free(stderr_buf);
		return FALSE;
	}

	strip_one_trailing_newline(stdout_buf);
	if (!stdout_buf || !stdout_buf[0]) {
		fprintf(stderr, "gpauth produced no authentication JSON\n");
		g_free(stdout_buf);
		g_free(stderr_buf);
		return FALSE;
	}

	*auth_json = stdout_buf;
	g_free(stderr_buf);
	return TRUE;
}

int
main(int argc, char **argv)
{
	char *vpn_name = NULL;
	char *vpn_uuid = NULL;
	char *vpn_service = NULL;
	gboolean allow_interaction = FALSE;
	GHashTable *options = NULL;
	GHashTable *secrets = NULL;
	const char *portal;
	const char *gateway;
	const char *browser;
	const char *auth_server;
	gboolean as_gateway;
	char *auth_json = NULL;
	int opt;
	int param_fd = dup(1);
	FILE *paramf = fdopen(param_fd, "w");

	/* Keep stdout reserved for NM's key/value response protocol. */
	dup2(2, 1);

	while ((opt = getopt_long(argc, argv, "ru:n:s:i", long_options, NULL)) >= 0) {
		switch (opt) {
		case 'r':
			break;
		case 'i':
			allow_interaction = TRUE;
			break;
		case 'u':
			vpn_uuid = optarg;
			break;
		case 'n':
			vpn_name = optarg;
			break;
		case 's':
			vpn_service = optarg;
			break;
		default:
			fprintf(stderr, "Unknown option\n");
			return 1;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Superfluous command line options\n");
		return 1;
	}

	if (!vpn_uuid || !vpn_name || !vpn_service) {
		fprintf(stderr, "Have to supply UUID, name, and service\n");
		return 1;
	}

	if (strcmp(vpn_service, NM_VPN_SERVICE_TYPE_OPENCONNECT) != 0) {
		fprintf(stderr,
		        "This dialog only works with the '%s' service\n",
		        NM_VPN_SERVICE_TYPE_OPENCONNECT);
		return 1;
	}

	if (!nm_vpn_service_plugin_read_vpn_details(0, &options, &secrets)) {
		fprintf(stderr,
		        "Failed to read '%s' (%s) data and secrets from stdin.\n",
		        vpn_name,
		        vpn_uuid);
		return 1;
	}

	portal = lookup_nonempty(options, NM_OPENCONNECT_KEY_PORTAL);
	gateway = lookup_nonempty(options, NM_OPENCONNECT_KEY_GATEWAY);
	if (!gateway)
		gateway = lookup_nonempty(secrets, NM_OPENCONNECT_KEY_GATEWAY);
	browser = lookup_nonempty(options, NM_OPENCONNECT_KEY_BROWSER);
	if (!browser)
		browser = "default";

	as_gateway = is_yes(lookup_nonempty(options, NM_OPENCONNECT_KEY_AS_GATEWAY)) || !portal;
	auth_server = as_gateway ? gateway : portal;

	if (!auth_server) {
		fprintf(stderr, "No portal or gateway configured\n");
		return 1;
	}

	if (!allow_interaction) {
		const char *existing_cookie;

		existing_cookie = lookup_nonempty(secrets, NM_OPENCONNECT_KEY_COOKIE);
		if (!existing_cookie) {
			fprintf(stderr, "Interaction is required to run gpauth\n");
			return 1;
		}
		auth_json = g_strdup(existing_cookie);
	} else if (!run_gpauth(auth_server, as_gateway, browser, options, secrets, &auth_json)) {
		return 1;
	}

	if (gateway) {
		fprintf(paramf, "%s\n%s\n", NM_OPENCONNECT_KEY_GATEWAY, gateway);
	} else if (portal) {
		/* Portal auto-gateway mode does not need a fixed gateway, but the
		 * service accepts the portal value as a harmless placeholder secret. */
		fprintf(paramf, "%s\n%s\n", NM_OPENCONNECT_KEY_GATEWAY, portal);
	}
	fprintf(paramf, "%s\n%s\n", NM_OPENCONNECT_KEY_COOKIE, auth_json);
	fprintf(paramf, "\n\n");
	fflush(paramf);
	wait_for_quit_or_eof();

	g_free(auth_json);
	g_hash_table_unref(options);
	g_hash_table_unref(secrets);

	return 0;
}
