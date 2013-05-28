AC_PREREQ(2.59)
AC_INIT(gmediarender, 0.0.7-git, https://github.com/hzeller/gmrender-resurrect)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([1.8 dist-bzip2 no-dist-gzip check-news])

AC_SYS_LARGEFILE

# Checks for programs
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

EXTRA_GCC_DEBUG_CFLAGS="" 
EXTRA_GCC_DEBUG_CXXFLAGS=""
  
if test -n "$GCC"; then
   EXTRA_GCC_DEBUG_CFLAGS="$CFLAGS"
   EXTRA_GCC_DEBUG_CXXFLAGS="$CXXFLAGS"
   CFLAGS="-Wall -Wpointer-arith -Wmissing-prototypes -Wmissing-declarations -Wwrite-strings"
   CXXFLAGS="-Wall -Wpointer-arith"
fi  

AC_CHECK_FUNCS([asprintf])
AC_CHECK_LIB([m],[exp])

# Debugging
AC_ARG_ENABLE(debug,
        [  --enable-debug          enable debugging],,
        enable_debug=no)
if test "x$enable_debug" = "xyes"; then
        CFLAGS="$CFLAGS -g -O0 -Wall -Werror"
fi

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES(GLIB, glib-2.0 gthread-2.0, HAVE_GLIB=yes, HAVE_GLIB=no)

# This is a bit crude, someone with more configure-fu please fix :)
# We want either the new, or if that fails, the old version of gstreamer.
# There must be a better way than my attempt of having a nested test.
GST_REQS=0.10.1
GSTPLUG_REQS=0.10.1
GST_OLD_MAJORMINOR=0.10
GST_NEW_MAJORMINOR=1.0

AC_ARG_WITH( gstreamer,
  AC_HELP_STRING([--without-gstreamer],[compile without GStreamer support]),
  try_gstreamer=$withval, try_gstreamer=yes )
HAVE_GST=no
if test x$try_gstreamer = xyes; then
  dnl check for GStreamer
  PKG_CHECK_MODULES(GST, gstreamer-$GST_NEW_MAJORMINOR >= $GST_REQS,
    [
      HAVE_GST=yes
      AC_SUBST(GST_CFLAGS)
      AC_SUBST(GST_LIBS)

    ],
    [
      HAVE_GST=no
    ])

    if test x$HAVE_GST = xno; then
       # check for an old version.
       PKG_CHECK_MODULES(GST, gstreamer-$GST_OLD_MAJORMINOR >= $GST_REQS,
       [
         HAVE_GST=yes
         AC_SUBST(GST_CFLAGS)
         AC_SUBST(GST_LIBS)
       ],
       [
         HAVE_GST=no
       ])
    fi
fi
if test x$HAVE_GST = xyes; then
  AC_DEFINE(HAVE_GST, , [Use GStreamer])
fi
AC_SUBST(HAVE_GST)
AM_CONDITIONAL(HAVE_GST, test x$HAVE_GST = xyes)


LIBUPNP_REQUIRED=1.6.0
AC_ARG_WITH( libupnp,
  AC_HELP_STRING([--without-libupnp],[compile without libupnp support]),
  try_libupnp=$withval, try_libupnp=yes )
HAVE_LIBUPNP=no
if test x$try_libupnp = xyes; then
  dnl check for libupnp
  PKG_CHECK_MODULES(LIBUPNP, libupnp >= $LIBUPNP_REQUIRED,
    [
      HAVE_LIBUPNP=yes
      AC_SUBST(LIBUPNP_CFLAGS)
      AC_SUBST(LIBUPNP_LIBS)

    ],
    [
      HAVE_LIBUPNP=no
    ])
fi
if test x$HAVE_LIBUPNP = xyes; then
  AC_DEFINE(HAVE_LIBUPNP, , [Use libupnp])
fi
AC_SUBST(HAVE_LIBUPNP)

# Checks for header files.
AC_HEADER_STDC

dnl Give error and exit if we don't have any UPnP SDK
if test "x$HAVE_LIBUPNP" = "xno"; then
  if test "x$HAVE_LIBGUPNP" = "xno"; then
    AC_MSG_ERROR(you need either libupnp or libgupnp)
  fi
fi

AC_CONFIG_FILES([Makefile
                 src/Makefile
		 data/Makefile])
AC_OUTPUT

