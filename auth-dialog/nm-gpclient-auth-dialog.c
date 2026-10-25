/*
 * Minimal NetworkManager auth dialog for gpclient.
 *
 * This runs in the user's session, invokes gpauth for browser-based
 * GlobalProtect authentication, and returns the resulting JSON as the VPN
 * "cookie" secret consumed by nm-gpclient-service.
 */

#include "nm-default.h"
#include "nm-gpclient-version.h"
#include "nm-gpclient-paths.h"

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <NetworkManager.h>

static volatile sig_atomic_t interrupted;

static void
auth_interrupted(int signum)
{
	interrupted = signum;
}

static void
auth_child_setup(gpointer data)
{
	/* Also release the callback lock if the desktop kills our dialog outright.
	 * Check for the parent dying between fork() and prctl(). */
	if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != *(pid_t *) data)
		_exit(1);
}

static gboolean
collect_auth(char **argv, char **output, char **errors, gint *status, GError **error)
{
	GPid pid;
	pid_t parent = getpid();
	struct pollfd fds[3] = {
		{ STDIN_FILENO, POLLIN, 0 }, { -1, POLLIN, 0 }, { -1, POLLIN, 0 }
	};
	GString *buffers[2];
	char line[16];
	gsize len = 0;
	gboolean cancelled = FALSE, exited = FALSE, ok = TRUE;
	gint64 kill_at = 0;
	int i;

	if (interrupted)
		return FALSE;
	if (!g_spawn_async_with_pipes(NULL, argv, NULL,
	                              G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
	                              auth_child_setup, &parent, &pid, NULL,
	                              &fds[1].fd, &fds[2].fd, error))
		return FALSE;
	for (i = 0; i < 2; i++)
		buffers[i] = g_string_new(NULL);

	/* Drain both output pipes while continuing to accept NM's cancellation.
	 * Do not signal a process group: the browser belongs to the user's session. */
	while (!exited || fds[1].fd >= 0 || fds[2].fd >= 0) {
		int ready = poll(fds, G_N_ELEMENTS(fds), 100);

		if (ready < 0 && errno != EINTR) {
			ok = FALSE;
			cancelled = TRUE;
		}
		if (interrupted)
			cancelled = TRUE;
		if (ready > 0 && fds[0].revents) {
			char c;
			ssize_t n = read(STDIN_FILENO, &c, 1);

			if (n == 0 || (n < 0 && errno != EINTR))
				cancelled = TRUE;
			else if (n == 1) {
				if (c == '\0' || c == '\n') {
					line[len] = '\0';
					if (!strcmp(line, "QUIT"))
						cancelled = TRUE;
					len = 0;
				} else if (len + 1 < sizeof(line))
					line[len++] = c;
			}
		}
		for (i = 1; i < 3; i++) {
			if (ready > 0 && fds[i].fd >= 0 && fds[i].revents) {
				char buf[4096];
				ssize_t n = read(fds[i].fd, buf, sizeof(buf));

				if (n > 0)
					g_string_append_len(buffers[i - 1], buf, n);
				else if (n == 0 || errno != EINTR) {
					if (n < 0)
						ok = FALSE;
					close(fds[i].fd);
					fds[i].fd = -1;
				}
			}
		}
		if (cancelled) {
			fds[0].fd = -1;
			if (!exited && !kill_at) {
				kill(pid, SIGTERM);
				kill_at = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
			} else if (!exited && g_get_monotonic_time() >= kill_at)
				kill(pid, SIGKILL);
		}
		if (!exited) {
			pid_t result = waitpid(pid, status, WNOHANG);

			if (result == pid)
				exited = TRUE;
			else if (result < 0 && errno != EINTR) {
				exited = TRUE;
				ok = FALSE;
			}
		}
		if (cancelled && exited)
			break;
	}
	for (i = 1; i < 3; i++) {
		if (fds[i].fd >= 0)
			close(fds[i].fd);
	}
	g_spawn_close_pid(pid);
	*output = g_string_free(buffers[0], FALSE);
	*errors = g_string_free(buffers[1], FALSE);
	if (cancelled)
		fprintf(stderr, "Authentication cancelled\n");
	return ok && !cancelled;
}

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

static void
add_optional_gpauth_setting_arg(GPtrArray *argv, GHashTable *options, const char *key, const char *arg)
{
	add_optional_arg(argv, arg, lookup_nonempty(options, key));
}

static void
add_optional_gpauth_secret_arg(GPtrArray *argv,
                               GHashTable *options,
                               GHashTable *secrets,
                               const char *key,
                               const char *arg)
{
	add_optional_arg(argv, arg, lookup_secret_or_option(options, secrets, key));
}

static void
add_gpauth_boolean_arg(GPtrArray *argv, GHashTable *options, const char *key, const char *arg)
{
	if (!is_yes(lookup_nonempty(options, key)))
		return;

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

	while (!interrupted && poll(&pfd, 1, 30000) > 0) {
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

	if (!nm_gpclient_check_version (NM_GPAUTH_BINARY, "gpauth", &error)) {
		fprintf(stderr, "%s\n", error->message);
		g_clear_error(&error);
		return FALSE;
	}

	argv = g_ptr_array_new();
	g_ptr_array_add(argv, NM_GPAUTH_BINARY);
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

	ok = collect_auth((char **) argv->pdata, &stdout_buf, &stderr_buf,
	                  &exit_status, &error);
	g_ptr_array_free(argv, TRUE);

	if (!ok) {
		if (error)
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
	struct sigaction action = { 0 };

	action.sa_handler = auth_interrupted;
	sigemptyset(&action.sa_mask);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGHUP, &action, NULL);

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
	if (interrupted) {
		g_free(auth_json);
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
