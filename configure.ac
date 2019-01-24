AC_INIT([nio-tlsf], [0.3], [akritid@niometrics.com])

# Library version information. This is different from the package version
# defined above. Before updating, please read:
# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
TLSF_CURRENT=1
TLSF_REVISION=0
TLSF_AGE=0

AC_SUBST(TLSF_CURRENT)
AC_SUBST(TLSF_REVISION)
AC_SUBST(TLSF_AGE)

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign subdir-objects silent-rules])
AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([tlsf.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC_C99
LT_INIT

AC_CONFIG_FILES([nio-tlsf.pc Makefile])
AC_OUTPUT
