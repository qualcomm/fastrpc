# Define the test programs
bin_PROGRAMS = fastrpc_test fastrpc-healthcheck

# ------------------------
# fastrpc_selftest (hardware-independent, for make check)
# ------------------------

if ANDROID_CC
else
check_PROGRAMS = fastrpc_selftest
TESTS = fastrpc_selftest

fastrpc_selftest_SOURCES = fastrpc_selftest.c
fastrpc_selftest_CFLAGS = -I$(top_srcdir)/inc
fastrpc_selftest_LDADD = $(top_builddir)/src/libcdsprpc.la -ldl
endif

# ------------------------
# fastrpc_test binary
# ------------------------

# Define the source files for the test program
fastrpc_test_SOURCES = fastrpc_test.c

# Define the compiler flags for the test program
fastrpc_test_CFLAGS = -I$(top_srcdir)/inc -DUSE_SYSLOG \
                      -Dtestlibdir=\"$(testlibdir)\" \
                      -Dtestdspdir=\"$(testdspdir)\"

if ANDROID_CC
USE_LOG = -llog
endif

# Define the linker flags for the test program
fastrpc_test_LDADD = -ldl $(USE_LOG)

# If compiling for Android, include the Android-specific flags
if ANDROID_CC
fastrpc_test_CFLAGS += -DANDROID
endif

# ------------------------
# fastrpc-healthcheck binary
# ------------------------

# Source file
fastrpc_healthcheck_SOURCES = fastrpc_healthcheck.c

# Base compiler flags (always)
fastrpc_healthcheck_CFLAGS = -I$(top_srcdir)/inc -DUSE_SYSLOG \
                             -DCONFIG_BASE_DIR='"$(CONFIG_BASE_DIR)"'

if ANDROID_CC
fastrpc_healthcheck_CFLAGS += -DANDROID
else
fastrpc_healthcheck_CFLAGS += -DPARSE_YAML @YAML_CFLAGS@
endif

# Linker flags
if ANDROID_CC
fastrpc_healthcheck_LDADD = -ldl $(USE_LOG)
else
fastrpc_healthcheck_LDADD = -ldl @YAML_LIBS@
endif


# ------------------------
# Test libraries and DSP artifacts
# ------------------------

TESTLIST = \
	calculator \
	hap_example \
	multithreading

DSPS = v68 v75

testlibdir = $(libdir)/fastrpc_test
dist_testlib_DATA = $(foreach test,$(TESTLIST),linux/lib$(test).so)

testdspdir = $(datadir)/fastrpc_test
nobase_dist_testdsp_DATA = $(foreach test,$(TESTLIST),$(foreach dsp,$(DSPS),$(dsp)/lib$(test)_skel.so))
