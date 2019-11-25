AC_PREREQ([2.64])
AC_INIT([kalibrate], [0.4.1], [jl@thre.at], [kal])
AC_CONFIG_SRCDIR([src/fcch_detector.cc])
AM_INIT_AUTOMAKE([-Wall -Werror])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LN_S
AC_PROG_RANLIB

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h sys/time.h unistd.h libgen.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE

# Checks for library functions.
AC_FUNC_STRTOD
AC_CHECK_FUNCS([floor getpagesize memset sqrt strtoul strtol])

# Checks for libraries.
AC_SEARCH_LIBS([basename], [rt])

PKG_CHECK_MODULES(FFTW3, fftw3 >= 3.0)
AC_SUBST(FFTW3_LIBS)
AC_SUBST(FFTW3_CFLAGS)

PKG_CHECK_MODULES(LIBRTLSDR, librtlsdr)
AC_SUBST(LIBRTLSDR_LIBS)
AC_SUBST(LIBRTLSDR_CFLAGS)
AC_CHECK_LIB([rtlsdr], [rtlsdr_set_dithering],
			 [AC_DEFINE([HAVE_DITHERING], [1], [Dithering available in rtlsdr])],
			 [AC_DEFINE([HAVE_DITHERING], [0], [Dithering not available in rtlsdr])])

# OSX doesn't support System V shared memory
AC_CANONICAL_HOST
case "$host_os" in
	darwin*)
		AC_DEFINE([D_HOST_OSX], [], [building for OSX])
		LRT_FLAGS=""
		AC_SUBST(LRT_FLAGS)
		;;
	openbsd*)
		AC_DEFINE([D_HOST_OPENBSD], [], [building for OpenBSD])
		;;
	*)
		LRT_FLAGS="-lrt"
		AC_SUBST(LRT_FLAGS)
		;;
esac

case "$host_cpu" in
	arm*)
		AC_DEFINE([D_HOST_OSX], [], [building without shared memory])
		;;
esac

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
