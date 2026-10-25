/* Runtime prerequisite shared by the service and desktop auth helper. */
#ifndef NM_GPCLIENT_VERSION_H
#define NM_GPCLIENT_VERSION_H

#include <string.h>
#include <sys/wait.h>

static inline gboolean
nm_gpclient_check_version (const char *binary, const char *program, GError **error)
{
	char *argv[] = { (char *) binary, "--version", NULL };
	char *output = NULL;
	char *prefix;
	gint status;
	gboolean supported;

	if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
	                   NULL, NULL, &output, NULL, &status, error))
		return FALSE;

	prefix = g_strdup_printf ("%s 3.", program);
	supported = WIFEXITED (status) && WEXITSTATUS (status) == 0 &&
	            output && g_str_has_prefix (output, prefix) &&
	            g_ascii_isdigit (output[strlen (prefix)]);
	g_free (prefix);
	g_free (output);
	if (!supported)
		g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
		             "%s 3.x is required; install matching upstream gpclient and gpauth 3.x binaries.",
		             program);
	return supported;
}

#endif
