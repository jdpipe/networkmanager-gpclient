/* Exercise libnm Disconnect/quit and the real child watch without a VPN or bus. */
int gpclient_service_main (int argc, char *argv[]);
#define main gpclient_service_main
#include "../src/nm-gpclient-service.c"
#undef main
#include <sys/prctl.h>

static guint failures;
static guint failure_reason;
static gboolean timed_out;

static void
child_interrupt (int signo)
{
	/* Shutdown can return a nonzero code without being a connection failure. */
	_exit (2);
}

static GPid
start_child (gboolean ignore_interrupt)
{
	int fds[2];
	GPid pid;
	char ready;

	g_assert_cmpint (pipe (fds), ==, 0);
	pid = fork ();
	g_assert_cmpint (pid, >=, 0);
	if (pid == 0) {
		close (fds[0]);
		if (prctl (PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid () == 1)
			_exit (99);
		if (setpgid (0, 0) < 0)
			_exit (99);
		signal (SIGINT, ignore_interrupt ? SIG_IGN : child_interrupt);
		if (write (fds[1], "!", 1) != 1)
			_exit (99);
		close (fds[1]);
		for (;;)
			pause ();
	}
	close (fds[1]);
	g_assert_cmpint (read (fds[0], &ready, 1), ==, 1);
	close (fds[0]);
	return pid;
}

static void
failure_cb (NMVpnServicePlugin *plugin, guint reason, gpointer data)
{
	failures++;
	failure_reason = reason;
}

static gboolean
request_disconnect (gpointer data)
{
	GError *error = NULL;

	g_assert (nm_vpn_service_plugin_disconnect (data, &error));
	g_assert_no_error (error);
	return FALSE;
}

static gboolean
test_timeout (gpointer data)
{
	timed_out = TRUE;
	g_main_loop_quit (gl.loop);
	return FALSE;
}

static void
exercise_disconnect (gboolean ignore_interrupt)
{
	NMVpnServicePlugin *plugin = g_object_new (NM_TYPE_OPENCONNECT_PLUGIN,
	                                         NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, TRUE,
	                                         NULL);
	NMOpenconnectPluginPrivate *priv = NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin);
	GPid unrelated = start_child (FALSE);
	guint watchdog;
	NMVpnServiceState state;

	failures = 0;
	timed_out = FALSE;
	gl.loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (plugin, "failure", G_CALLBACK (failure_cb), NULL);
	g_signal_connect (plugin, "quit", G_CALLBACK (quit_mainloop), gl.loop);
	priv->pid = start_child (ignore_interrupt);
	g_child_watch_add (priv->pid, openconnect_watch_cb, plugin);
	g_object_set (plugin, NM_VPN_SERVICE_PLUGIN_STATE, NM_VPN_SERVICE_STATE_STARTED, NULL);
	g_idle_add (request_disconnect, plugin);
	watchdog = g_timeout_add_seconds (5, test_timeout, NULL);
	g_main_loop_run (gl.loop);
	g_assert (!timed_out);
	g_source_remove (watchdog);
	g_assert_cmpuint (failures, ==, 0);
	g_assert_cmpint (priv->pid, ==, 0);
	g_assert_cmpuint (priv->kill_timeout, ==, 0);
	g_assert (priv->quit_pending);
	g_assert (!priv->disconnect_requested);
	g_object_get (plugin, NM_VPN_SERVICE_PLUGIN_STATE, &state, NULL);
	g_assert_cmpint (state, ==, NM_VPN_SERVICE_STATE_STOPPED);
	/* Disconnect must affect only our child's process group. */
	g_assert_cmpint (kill (unrelated, 0), ==, 0);
	kill (unrelated, SIGKILL);
	waitpid (unrelated, NULL, 0);
	g_main_loop_unref (gl.loop);
	gl.loop = NULL;
	g_object_unref (plugin);
}

static void
test_graceful_disconnect (void)
{
	exercise_disconnect (FALSE);
}

static void
test_forced_disconnect (void)
{
	exercise_disconnect (TRUE);
}

static void
test_unexpected_exit (void)
{
	NMVpnServicePlugin *plugin = g_object_new (NM_TYPE_OPENCONNECT_PLUGIN,
	                                         NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, TRUE,
	                                         NULL);
	NMOpenconnectPluginPrivate *priv = NM_OPENCONNECT_PLUGIN_GET_PRIVATE (plugin);
	guint watchdog;

	failures = 0;
	timed_out = FALSE;
	gl.loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (plugin, "failure", G_CALLBACK (failure_cb), NULL);
	g_signal_connect (plugin, "quit", G_CALLBACK (quit_mainloop), gl.loop);
	priv->pid = start_child (FALSE);
	g_child_watch_add (priv->pid, openconnect_watch_cb, plugin);
	g_object_set (plugin, NM_VPN_SERVICE_PLUGIN_STATE, NM_VPN_SERVICE_STATE_STARTED, NULL);
	/* An exit without a NetworkManager Disconnect must still report failure. */
	kill (priv->pid, SIGINT);
	watchdog = g_timeout_add_seconds (5, test_timeout, NULL);
	g_main_loop_run (gl.loop);
	g_assert (!timed_out);
	g_source_remove (watchdog);
	g_assert_cmpuint (failures, ==, 1);
	g_assert_cmpuint (failure_reason, ==, NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED);
	g_assert_cmpint (priv->pid, ==, 0);
	g_main_loop_unref (gl.loop);
	gl.loop = NULL;
	g_object_unref (plugin);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/disconnect/graceful-exit-without-failure", test_graceful_disconnect);
	g_test_add_func ("/disconnect/forced-exit-without-failure", test_forced_disconnect);
	g_test_add_func ("/disconnect/unexpected-exit-reports-failure", test_unexpected_exit);
	return g_test_run ();
}
