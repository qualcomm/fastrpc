# Define the test programs
bin_PROGRAMS = fastrpc_test fastrpc-healthcheck

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
                             -DCONFIG_BASE_DIR='"$(CONFIG_BASE_DIR)"' \
                             -DLIBDIR='"$(libdir)"'

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
