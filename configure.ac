AC_PREREQ(2.52)

AC_INIT(NetworkManager-gpclient,
        0.1.0,
        https://github.com/yuezk/GlobalProtect-openconnect,
        NetworkManager-gpclient)
AM_INIT_AUTOMAKE([no-dist-gzip dist-xz])
AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

AC_USE_SYSTEM_EXTENSIONS

dnl
dnl Require programs
dnl
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PATH_PROG(GLIB_COMPILE_RESOURCES, glib-compile-resources)

AC_GNU_SOURCE

AC_CHECK_PROG([has_file], file, yes, no)
if test x$has_file = xno ; then
    AC_MSG_ERROR(["file" utility not found.])
fi

AC_CHECK_PROG([has_find], find, yes, no)
if test x$has_find = xno ; then
    AC_MSG_ERROR(["find" utility not found.])
fi

dnl
dnl Required headers
dnl
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h paths.h sys/ioctl.h sys/time.h syslog.h unistd.h)

dnl
dnl Checks for typedefs, structures, and compiler characteristics.
dnl
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_HEADER_TIME

dnl
dnl Checks for library functions.
dnl
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MEMCMP
AC_CHECK_FUNCS(select socket uname)

GIT_SHA_RECORD(NM_GIT_SHA)

AC_SEARCH_LIBS([dlopen], [dl dld], [], [ac_cv_search_dlopen=])
AS_IF([test "x$ac_cv_search_dlopen" = "xnone required"],
      [DL_LIBS=],
      [DL_LIBS="$ac_cv_search_dlopen"])
AC_SUBST([DL_LIBS])

dnl ensure that when the Automake generated makefile calls aclocal,
dnl it honours the $ACLOCAL_FLAGS environment variable
ACLOCAL_AMFLAGS="\${ACLOCAL_FLAGS}"
if test -n "$ac_macro_dir"; then
    ACLOCAL_AMFLAGS="-I $ac_macro_dir $ACLOCAL_AMFLAGS"
fi
AC_SUBST([ACLOCAL_AMFLAGS])

dnl
dnl GNOME support
dnl
AC_ARG_WITH(gnome, AS_HELP_STRING([--without-gnome], [Build NetworkManager-gpclient without GNOME support, e.g. vpn service only]), [], [with_gnome_specified=no])
AC_ARG_WITH(gtk4, AS_HELP_STRING([--with-gtk4], [Build NetworkManager-gpclient with libnma-gtk4 support]), [], [with_gtk4_specified=no])
AC_ARG_WITH(libnm-glib, AS_HELP_STRING([--with-libnm-glib], [Build NetworkManager-gpclient with libnm-glib compatibility (deprecated)]), [], [with_libnm_glib_specified=no])
if test "$with_gnome_specified" == no; then
	with_gnome=no
fi
if test "$with_libnm_glib_specified" != no -a "$with_libnm_glib" != no; then
	if test "$with_gnome_specified" != no -a "$with_gnome" == no; then
		AC_MSG_ERROR(Building --with-libnm-glib conflicts with --without-gnome)
	fi
fi
if test "$with_gnome" != no; then
	with_gnome=yes
fi
if test "$with_gtk4_specified" == no; then
	with_gtk4=no
fi
if test "$with_gtk4" != yes; then
	with_gtk4=no
fi
if test "$with_libnm_glib_specified" == no; then
	with_libnm_glib=no
fi
if test "$with_libnm_glib" != yes; then
	with_libnm_glib=no
fi
AM_CONDITIONAL(WITH_GNOME, test "$with_gnome" != no)
AM_CONDITIONAL(WITH_GTK4, test "$with_gtk4" != no)
AM_CONDITIONAL(WITH_LIBNM_GLIB, test "$with_libnm_glib" != no)

AC_ARG_ENABLE(absolute-paths, AS_HELP_STRING([--enable-absolute-paths], [Use an absolute editor plugin path in .name files (default: no)]), [], [enable_absolute_paths=no])

GETTEXT_PACKAGE=NetworkManager-gpclient
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])

PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.34)
GLIB_CFLAGS="$GLIB_CFLAGS -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_34"
GLIB_CFLAGS="$GLIB_CFLAGS -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_34"

PKG_CHECK_MODULES(GMODULE, gmodule-2.0)
PKG_CHECK_MODULES(LIBXML, libxml-2.0)

if test x"$with_gnome" != xno; then
	PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.12)
	GTK_CFLAGS="$GTK_CFLAGS -DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_12"
	GTK_CFLAGS="$GTK_CFLAGS -DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_12"
	PKG_CHECK_MODULES(LIBNMA, libnma >= 1.2.0)

	if test x"$with_libnm_glib" != xno; then
		PKG_CHECK_MODULES(LIBNM_GLIB,
			NetworkManager >= 1.2.0
			libnm-util >= 1.2.0
			libnm-glib >= 1.2.0
			libnm-glib-vpn >= 1.2.0);

		LIBNM_GLIB_CFLAGS="$LIBNM_GLIB_CFLAGS -DNM_VERSION_MIN_REQUIRED=NM_VERSION_1_2"
		LIBNM_GLIB_CFLAGS="$LIBNM_GLIB_CFLAGS -DNM_VERSION_MAX_ALLOWED=NM_VERSION_1_4"
	fi
fi

if test x"$with_gtk4" != xno; then
	PKG_CHECK_MODULES(GTK4, gtk4 >= 4.0)
	GTK4_CFLAGS="$GTK4_CFLAGS -DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_0"
	GTK4_CFLAGS="$GTK4_CFLAGS -DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_0"
fi

PKG_CHECK_MODULES(LIBNM, libnm >= 1.2.0)
LIBNM_CFLAGS="$LIBNM_CFLAGS -DNM_VERSION_MIN_REQUIRED=NM_VERSION_1_2"
LIBNM_CFLAGS="$LIBNM_CFLAGS -DNM_VERSION_MAX_ALLOWED=NM_VERSION_1_4"

AC_ARG_WITH(authdlg, AS_HELP_STRING([--without-authdlg], [Build NetworkManager-gpclient without authentication dialog]), [], [with_authdlg=yes])
if test x"$with_authdlg" != xno; then
	with_authdlg=yes
fi
AM_CONDITIONAL(WITH_AUTHDLG, test x"$with_authdlg" != xno)

AC_ARG_WITH(sso-mib, AS_HELP_STRING([--with-sso-mib], [Build with sso-mib support for Entra Conditional Access (default: auto)]),
	    [], [with_sso_mib=auto])
if test x"$with_sso_mib" != xno; then
	PKG_CHECK_MODULES(SSO_MIB, sso-mib,
			  [with_sso_mib=yes],
			  [if test x"$with_sso_mib" = xyes; then
				AC_MSG_ERROR([sso-mib requested but not found])
			   fi
			   with_sso_mib=no])
fi
if test x"$with_sso_mib" = xyes; then
	AC_DEFINE(WITH_SSO_MIB, 1, [Define to enable sso-mib for Entra Conditional Access])
fi
AM_CONDITIONAL(WITH_SSO_MIB, test x"$with_sso_mib" = xyes)

AC_ARG_VAR([GPCLIENT_BINARY], [Absolute path to the gpclient executable])
AC_ARG_WITH([gpclient], AS_HELP_STRING([--with-gpclient=PATH], [Absolute path to gpclient]), [GPCLIENT_BINARY=$withval])
AC_PATH_PROG([GPCLIENT_BINARY], [gpclient], [/usr/bin/gpclient], [$PATH:/usr/local/bin:/usr/bin:/opt/bin])
AC_ARG_VAR([GPAUTH_BINARY], [Absolute path to the gpauth executable])
AC_ARG_WITH([gpauth], AS_HELP_STRING([--with-gpauth=PATH], [Absolute path to gpauth]), [GPAUTH_BINARY=$withval])
AC_PATH_PROG([GPAUTH_BINARY], [gpauth], [/usr/bin/gpauth], [$PATH:/usr/local/bin:/usr/bin:/opt/bin])
AC_ARG_VAR([MODPROBE], [Absolute path to modprobe, or no to disable module loading])
AC_ARG_WITH([modprobe], AS_HELP_STRING([--with-modprobe=PATH], [Absolute path to modprobe; --without-modprobe disables module loading]), [MODPROBE=$withval])
AS_IF([test "x$MODPROBE" = xno], [MODPROBE=], [
	AC_PATH_PROG([MODPROBE], [modprobe], [], [$PATH:/usr/sbin:/sbin])
])
AC_ARG_VAR([GPCLIENT_LOCK_FILE], [Absolute path to the shared gpclient PID lock file])
AC_ARG_WITH([gpclient-lock-file], AS_HELP_STRING([--with-gpclient-lock-file=PATH], [gpclient PID lock file (default: /run/gpclient.lock)]), [GPCLIENT_LOCK_FILE=$withval])
AS_IF([test -z "$GPCLIENT_LOCK_FILE"], [GPCLIENT_LOCK_FILE=/run/gpclient.lock])
AS_CASE([$GPCLIENT_BINARY], [/*], [], [AC_MSG_ERROR([gpclient path must be absolute])])
AS_CASE([$GPAUTH_BINARY], [/*], [], [AC_MSG_ERROR([gpauth path must be absolute])])
AS_CASE([$MODPROBE], [/*|""], [], [AC_MSG_ERROR([modprobe path must be absolute, or use --without-modprobe])])
AS_CASE([$GPCLIENT_LOCK_FILE], [/*], [], [AC_MSG_ERROR([gpclient lock file must be absolute])])
AC_DEFINE_UNQUOTED([NM_GPCLIENT_BINARY_DEFAULT], ["$GPCLIENT_BINARY"], [Configured gpclient executable])
AC_DEFINE_UNQUOTED([NM_GPAUTH_BINARY_DEFAULT], ["$GPAUTH_BINARY"], [Configured gpauth executable])
AC_DEFINE_UNQUOTED([NM_MODPROBE_DEFAULT], ["$MODPROBE"], [Configured module loader, empty if disabled])
AC_DEFINE_UNQUOTED([NM_GPCLIENT_LOCK_FILE_DEFAULT], ["$GPCLIENT_LOCK_FILE"], [Configured gpclient PID lock file])

AC_ARG_VAR([NM_VPN_SERVICE_DIR], [NetworkManager VPN service metadata directory])
AC_ARG_WITH([nm-vpn-service-dir], AS_HELP_STRING([--with-nm-vpn-service-dir=DIR], [NetworkManager VPN service metadata directory]), [NM_VPN_SERVICE_DIR=$withval])
AS_IF([test -z "$NM_VPN_SERVICE_DIR"], [NM_VPN_SERVICE_DIR=`$PKG_CONFIG --variable=vpnservicedir libnm`])
AS_IF([test -z "$NM_VPN_SERVICE_DIR"], [NM_VPN_SERVICE_DIR='${libdir}/NetworkManager/VPN'])
AC_SUBST(NM_VPN_SERVICE_DIR)

NM_COMPILER_WARNINGS

AC_ARG_VAR([NM_PLUGIN_DIR], [libnm VPN editor plugin directory])
AC_ARG_WITH([nm-plugin-dir], AS_HELP_STRING([--with-nm-plugin-dir=DIR], [libnm VPN editor plugin directory]), [NM_PLUGIN_DIR=$withval])
AS_IF([test -z "$NM_PLUGIN_DIR"], [NM_PLUGIN_DIR=`$PKG_CONFIG --variable=plugindir libnm`])
AS_IF([test -z "$NM_PLUGIN_DIR"], [NM_PLUGIN_DIR='${libdir}/NetworkManager'])
AC_SUBST(NM_PLUGIN_DIR)
if test x"$enable_absolute_paths" == x"yes"; then
	NM_PLUGIN_DIR_NAME_FILE="$NM_PLUGIN_DIR/"
else
	enable_absolute_paths=no
	NM_PLUGIN_DIR_NAME_FILE=""
fi
AC_SUBST(NM_PLUGIN_DIR_NAME_FILE)

dnl systemd-sysusers does not support locked user ('u!') before v257 and fails
PKG_CHECK_MODULES(SYSTEMD, systemd >= 257, [NM_USER_LOCKED_UNLOCKED="u!"], [NM_USER_LOCKED_UNLOCKED="u"])
AC_SUBST(NM_USER_LOCKED_UNLOCKED)

NM_SYSUSERS_DIR=`$PKG_CONFIG --define-variable prefix='\${prefix}' --variable sysusersdir systemd`
AC_SUBST(NM_SYSUSERS_DIR)

dnl
dnl Distribution version string
dnl
AC_ARG_WITH(dist-version, AS_HELP_STRING([--with-dist-version=<dist-version>], [Define the custom version (like distribution package name and revision)]), ac_distver=$withval, ac_distver="")
if ! test x"$ac_distver" = x""; then
  AC_DEFINE_UNQUOTED(DIST_VERSION, "$ac_distver", [Define the distribution version string])
fi

AC_CONFIG_FILES([
Makefile
])
AC_OUTPUT

echo ""
echo "Build configuration: "
echo "  --with-gnome=$with_gnome"
echo "  --with-gtk4=$with_gtk4"
echo "  --with-libnm-glib=$with_libnm_glib"
echo "  --with-authdlg=$with_authdlg"
echo "  --with-sso-mib=$with_sso_mib"
echo "  --enable-absolute-paths=$enable_absolute_paths"
echo "  --enable-more-warnings=$set_more_warnings"
echo "  gpclient=$GPCLIENT_BINARY"
echo "  gpauth=$GPAUTH_BINARY"
echo "  modprobe=$MODPROBE"
echo "  gpclient lock file=$GPCLIENT_LOCK_FILE"
echo "  libnm plugin directory=$NM_PLUGIN_DIR"
echo "  VPN service directory=$NM_VPN_SERVICE_DIR"
