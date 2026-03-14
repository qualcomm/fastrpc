#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([fastrpc], [0.0.1])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE(1.10 foreign subdir-objects)
LT_INIT(disable-static)

AS_CASE([$host],
  [*-linux-android], [
  compile_for_android=yes
  ], [
  compile_for_android=no
  ]
)

AM_CONDITIONAL([ANDROID_CC],
[test "$compile_for_android" = yes])

# Add shared object versioning
m4_define([LT_MAJOR], [1])
m4_define([LT_MINOR], [0])
m4_define([LT_PATCH], [0])

AC_SUBST([LT_VERSION], LT_MAJOR.LT_MINOR)
AC_SUBST([LT_VERSION_NUMBER], LT_MAJOR:LT_MINOR:LT_PATCH)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

# Checks for libraries.

# Enable pkg-config
PKG_PROG_PKG_CONFIG

# Configure systemd unit directory option (--with-systemdsystemunitdir)
AC_ARG_WITH([systemdsystemunitdir],
  [AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
                  [Directory for systemd service files (default: /lib/systemd/system)])],
  [systemdsystemunitdir="$withval"],
  [systemdsystemunitdir="/lib/systemd/system"])
AC_MSG_NOTICE([systemd unit directory: $systemdsystemunitdir])
AC_SUBST([systemdsystemunitdir])

# Configure udev rules directory using pkg-config
AC_ARG_WITH([udevrulesdir],
  [AS_HELP_STRING([--with-udevrulesdir=DIR],
                  [Directory for udev rules files])],
  [udevrulesdir="$withval"],
  [
    # Get udev directory from pkg-config
    PKG_CHECK_VAR([udevrulesdir], [udev], [udevdir],
      [udevrulesdir="${udevrulesdir}/rules.d"],
      [AC_MSG_ERROR([Could not determine udev rules directory. Please install udev development files or use --with-udevrulesdir=DIR])])
  ])
AC_MSG_NOTICE([udev rules directory: $udevrulesdir])
AC_SUBST([udevrulesdir])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

# Check for libyaml only if not Android
AS_IF([test "$compile_for_android" = no], [
  PKG_CHECK_MODULES([YAML], [yaml-0.1], [],
    [AC_MSG_ERROR([libyaml (yaml-0.1) is required but not found.])])
  AC_SUBST(YAML_CFLAGS)
  AC_SUBST(YAML_LIBS)
])

# Check for libbsd only if not Android (provides strlcpy, strlcat, etc.)
AS_IF([test "$compile_for_android" = no], [
  PKG_CHECK_MODULES([BSD], [libbsd-overlay], [],
    [AC_MSG_ERROR([libbsd (libbsd-overlay) is required but not found.])])
  AC_SUBST(BSD_CFLAGS)
  AC_SUBST(BSD_LIBS)
])

# Configure config base path option (--with-config-base-dir)
AC_ARG_WITH([config-base-dir],
  [AS_HELP_STRING([--with-config-base-dir=PATH],
                  [Base directory for config files (default: /usr/share/qcom)])],
  [config_base_dir="$withval"],
  [config_base_dir="/usr/share/qcom/"])
AC_MSG_NOTICE([Config base path: $config_base_dir])
AC_SUBST([CONFIG_BASE_DIR], ["$config_base_dir"])

AC_CONFIG_FILES([
Makefile
inc/Makefile
src/Makefile
test/Makefile
files/Makefile
])
AC_OUTPUT
