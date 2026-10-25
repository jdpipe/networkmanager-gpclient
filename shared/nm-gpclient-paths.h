/* Configured paths shared by the service, auth dialog, and editors.
 * Test binaries can override these without changing the installed binaries. */
#ifndef NM_GPCLIENT_PATHS_H
#define NM_GPCLIENT_PATHS_H

#ifndef NM_GPCLIENT_BINARY
#define NM_GPCLIENT_BINARY NM_GPCLIENT_BINARY_DEFAULT
#endif
#ifndef NM_GPAUTH_BINARY
#define NM_GPAUTH_BINARY NM_GPAUTH_BINARY_DEFAULT
#endif
#ifndef NM_MODPROBE
#define NM_MODPROBE NM_MODPROBE_DEFAULT
#endif
#ifndef NM_GPCLIENT_LOCK_FILE
#define NM_GPCLIENT_LOCK_FILE NM_GPCLIENT_LOCK_FILE_DEFAULT
#endif

#endif
