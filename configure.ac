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
  [*-linux-android*], [
  compile_for_android=yes
  ], [
  compile_for_android=no
  ]
)

AM_CONDITIONAL([ANDROID_CC],
[test "$compile_for_android" = yes])

# Optional standalone CMake base-test build.  Keep all of its prerequisites
# conditional so the normal FastRPC build remains self-contained.
AC_ARG_ENABLE([base-test],
  [AS_HELP_STRING([--enable-base-test],
                  [enable the CMake base-test sub-build])],
  [enable_base_test=$enableval],
  [enable_base_test=no])
AS_CASE([$enable_base_test],
  [yes|no], [],
  [AC_MSG_ERROR([bad value '$enable_base_test' for --enable-base-test])])

AC_ARG_VAR([QAIC_EXECUTABLE],
  [path to the QAIC compiler used by base-test (or qaic on PATH)])
AC_ARG_VAR([HEXAGON_CLANG],
  [Hexagon compiler used to build the base-test DSP skeleton])
AC_ARG_VAR([HEXAGON_LD],
  [Hexagon linker used to link the base-test DSP skeleton])
AC_ARG_VAR([DSP_ARCH],
  [Hexagon DSP architecture for base-test (default: 73)])
AC_ARG_VAR([ANDROID_NDK_HOME],
  [Android NDK root used to build the Android base-test])

AC_ARG_WITH([qaic],
  [AS_HELP_STRING([--with-qaic=PATH],
                  [path to the QAIC compiler used by base-test])],
  [QAIC_EXECUTABLE=$withval])

AC_ARG_WITH([dsp-arch],
  [AS_HELP_STRING([--with-dsp-arch=ARCH],
                  [Hexagon DSP architecture for base-test (default: 73)])],
  [base_test_dsp_arch=$withval],
  [AS_IF([test -n "$DSP_ARCH"],
         [base_test_dsp_arch=$DSP_ARCH],
         [base_test_dsp_arch=73])])

AC_ARG_VAR([CMAKE], [CMake executable used to build base-test])

base_test_toolchain_file=
base_test_cmake_extra_args=

AS_IF([test "x$enable_base_test" = xyes], [
  AS_CASE([$host_os],
    [linux-gnu*|linux-musl*|linux-android*], [],
    [AC_MSG_ERROR([base-test supports only Linux (glibc/musl) or Android targets; got $host])])
  AS_IF([test "x$host_cpu" != xaarch64], [
    AC_MSG_ERROR([base-test requires an aarch64 target; got $host])
  ])
  AS_IF([test "$compile_for_android" = yes], [
    AS_IF([test -z "$ANDROID_NDK_HOME"], [
      AC_MSG_ERROR([ANDROID_NDK_HOME must be set to build base-test for Android])
    ])
    base_test_ndk_home=`cd "$ANDROID_NDK_HOME" 2>/dev/null && pwd`
    AS_IF([test -z "$base_test_ndk_home"], [
      AC_MSG_ERROR([Android NDK directory not found: $ANDROID_NDK_HOME])
    ])
    base_test_toolchain_file="$base_test_ndk_home/build/cmake/android.toolchain.cmake"
    AS_IF([test ! -f "$base_test_toolchain_file"], [
      AC_MSG_ERROR([Android NDK toolchain file not found: $base_test_toolchain_file])
    ])
    unset base_test_ndk_home
    base_test_cmake_extra_args='-DANDROID_ABI=arm64-v8a'
  ], [
    base_test_srcdir=`cd "$srcdir" && pwd`
    base_test_toolchain_file="$base_test_srcdir/test/base_test/cmake/toolchain/aarch64-linux-gnu.cmake"
  ])
  AS_IF([test -z "$QAIC_EXECUTABLE"], [
    AC_PATH_PROG([QAIC_EXECUTABLE], [qaic], [no])
  ])
  AS_IF([test "x$QAIC_EXECUTABLE" = xno || test ! -x "$QAIC_EXECUTABLE"], [
    AC_MSG_ERROR([QAIC compiler is not executable: $QAIC_EXECUTABLE; set QAIC_EXECUTABLE or use --with-qaic=PATH])
  ])
  AS_IF([test -n "$HEXAGON_CLANG"], [
    AS_IF([test ! -x "$HEXAGON_CLANG" || test -d "$HEXAGON_CLANG"], [
      AC_MSG_ERROR([HEXAGON_CLANG is not an executable file: $HEXAGON_CLANG])
    ])
  ])
  AS_IF([test -n "$HEXAGON_LD"], [
    AS_IF([test ! -x "$HEXAGON_LD" || test -d "$HEXAGON_LD"], [
      AC_MSG_ERROR([HEXAGON_LD is not an executable file: $HEXAGON_LD])
    ])
  ])
  case "$base_test_dsp_arch" in
    ''|*[[!0123456789]]*)
      AC_MSG_ERROR([--with-dsp-arch must contain only decimal digits]) ;;
  esac
  AC_PATH_PROG([CMAKE], [cmake], [no])
  AS_IF([test "x$CMAKE" = xno], [
    AC_MSG_ERROR([cmake is required with --enable-base-test])
  ])
  AS_IF([test ! -f "$srcdir/test/base_test/CMakeLists.txt"], [
    AC_MSG_ERROR([test/base_test/CMakeLists.txt is missing])
  ])
  AS_IF([test ! -f "$srcdir/test/base_test/vendor/unity/CMakeLists.txt"], [
    AC_MSG_ERROR([base-test Unity submodule is missing; run git submodule update --init --recursive])
  ])
])

AM_CONDITIONAL([BASE_TEST_ENABLED],
  [test "x$enable_base_test" = xyes])
AC_SUBST([BASE_TEST_TOOLCHAIN_FILE], [$base_test_toolchain_file])
AC_SUBST([BASE_TEST_CMAKE_EXTRA_ARGS], [$base_test_cmake_extra_args])
AC_SUBST([BASE_TEST_DSP_ARCH], [$base_test_dsp_arch])
AC_SUBST([QAIC_EXECUTABLE])
AC_SUBST([HEXAGON_CLANG])
AC_SUBST([HEXAGON_LD])
AC_SUBST([ANDROID_NDK_HOME])
AC_SUBST([CMAKE])

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
  [PKG_CHECK_VAR([systemdsystemunitdir], [systemd], [systemdsystemunitdir], [], [systemdsystemunitdir="/lib/systemd/system"])])
AC_MSG_NOTICE([systemd unit directory: $systemdsystemunitdir])
AC_SUBST([systemdsystemunitdir])

# Configure udev rules directory option (--with-udevrulesdir)
AC_ARG_WITH([udevrulesdir],
  [AS_HELP_STRING([--with-udevrulesdir=DIR],
                  [Directory for udev rules files (default: /lib/udev/rules.d)])],
  [udevrulesdir="$withval"],
  [PKG_CHECK_VAR([udevdir], [udev], [udevdir], [udevrulesdir="${udevdir}/rules.d"], [udevrulesdir="/lib/udev/rules.d"])])
AC_MSG_NOTICE([udev rules directory: $udevrulesdir])
AC_SUBST([udevrulesdir])

# Configure sysusers directory option (--with-sysusersdir)
AC_ARG_WITH([sysusersdir],
  [AS_HELP_STRING([--with-sysusersdir=DIR],
                  [Directory for systemd sysusers files (default: /usr/lib/sysusers.d)])],
  [sysusersdir="$withval"],
  [PKG_CHECK_VAR([sysusersdir], [systemd], [sysusersdir], [], [sysusersdir="/usr/lib/sysusers.d"])])
AC_MSG_NOTICE([sysusers directory: $sysusersdir])
AC_SUBST([sysusersdir])

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

# Enable stricter compiler flags
# These are based on Debian's dpkg-buildflags defaults for compatibility with
# distro builds and to catch issues during development
AC_ARG_ENABLE([stricter],
  [AS_HELP_STRING([--enable-stricter],
                  [Enable stricter compiler flags (warnings, stack protection, fortify source, RELRO)])],
  [enable_stricter=$enableval],
  [enable_stricter=no])

# Helper macro to test if compiler accepts a flag
AC_DEFUN([CC_CHECK_FLAG], [
  AC_MSG_CHECKING([if $CC supports $1])
  saved_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $1"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
    [AC_MSG_RESULT([yes]); $2],
    [AC_MSG_RESULT([no]); $3])
  CFLAGS="$saved_CFLAGS"
])

# Helper macro to test if linker accepts a flag
AC_DEFUN([LD_CHECK_FLAG], [
  AC_MSG_CHECKING([if linker supports $1])
  saved_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $1"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
    [AC_MSG_RESULT([yes]); $2],
    [AC_MSG_RESULT([no]); $3])
  LDFLAGS="$saved_LDFLAGS"
])

# Set up hardening flags
STRICTER_CFLAGS=""
STRICTER_CPPFLAGS=""
STRICTER_LDFLAGS=""
AS_IF([test "x$enable_stricter" = "xyes"], [
  # Format string security warnings
  STRICTER_CFLAGS="-Wformat -Werror=format-security -Werror=implicit-function-declaration"
  
  # Stack protection
  CC_CHECK_FLAG([-fstack-protector-strong],
    [STRICTER_CFLAGS="$STRICTER_CFLAGS -fstack-protector-strong"])
  CC_CHECK_FLAG([-fstack-clash-protection],
    [STRICTER_CFLAGS="$STRICTER_CFLAGS -fstack-clash-protection"])
  
  # Architecture-specific hardening
  AS_CASE([$host_cpu],
    [aarch64], [
      CC_CHECK_FLAG([-mbranch-protection=standard],
        [STRICTER_CFLAGS="$STRICTER_CFLAGS -mbranch-protection=standard"])
    ],
    []
  )
  
  # FORTIFY_SOURCE and reproducible builds
  STRICTER_CPPFLAGS="-D_FORTIFY_SOURCE=2 -Wdate-time"
  
  # RELRO - read-only relocations
  LD_CHECK_FLAG([-Wl,-z,relro],
    [STRICTER_LDFLAGS="-Wl,-z,relro"])
  
  AC_MSG_NOTICE([Stricter compiler flags enabled)])
  AC_MSG_NOTICE([  STRICTER_CFLAGS: $STRICTER_CFLAGS])
  AC_MSG_NOTICE([  STRICTER_CPPFLAGS: $STRICTER_CPPFLAGS])
  AC_MSG_NOTICE([  STRICTER_LDFLAGS: $STRICTER_LDFLAGS])
], [
  AC_MSG_NOTICE([Stricter compiler flags disabled (use --enable-stricter to enable)])
])
AC_SUBST([STRICTER_CFLAGS])
AC_SUBST([STRICTER_CPPFLAGS])
AC_SUBST([STRICTER_LDFLAGS])


AC_CONFIG_FILES([
Makefile
inc/Makefile
src/Makefile
test/Makefile
files/Makefile
Docs/manpages/Makefile
])
AC_OUTPUT
