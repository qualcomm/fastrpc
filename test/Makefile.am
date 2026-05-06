# Define the test programs
bin_PROGRAMS = fastrpc_test dsp_check

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
# dsp_check binary
# ------------------------

# Define the source files for dsp_check
dsp_check_SOURCES = dsp_check.c

# Define the compiler flags for dsp_check
dsp_check_CFLAGS = -I$(top_srcdir)/inc -DUSE_SYSLOG

# Define the linker flags for dsp_check (needs libyaml)
dsp_check_LDADD = -lyaml -ldl

if ANDROID_CC
dsp_check_CFLAGS += -DANDROID
endif

# ------------------------
# Test libraries and DSP artifacts (unchanged)
# ------------------------

TESTLIST = \
	calculator \
	hap_example \
	multithreading

DSPS = v68 v75

testlibdir = $(libdir)/fastrpc_test
dist_testlib_DATA = $(foreach test,$(TESTLIST),linux/lib$(test).so)

testdspdir = $(datadir)/fastrpc_test
nobase_dist_testdsp_DATA = \
    $(foreach test,$(TESTLIST),$(foreach dsp,$(DSPS),$(dsp)/lib$(test)_skel.so))
