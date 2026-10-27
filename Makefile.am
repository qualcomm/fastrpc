SUBDIRS = inc src test files Docs/manpages

ACLOCAL_AMFLAGS = -I m4

BASE_TEST_SRCDIR = $(abs_top_srcdir)/test/base_test
BASE_TEST_BUILDDIR = $(abs_top_builddir)/test/base_test/builddir
BASE_TEST_STAGEDIR = $(BASE_TEST_BUILDDIR)/_stage
BASE_TEST_CMAKE = @CMAKE@
BASE_TEST_QAIC = @QAIC_EXECUTABLE@
BASE_TEST_HEXAGON_CLANG = @HEXAGON_CLANG@
BASE_TEST_HEXAGON_LD = @HEXAGON_LD@
BASE_TEST_DSP_ARCH = @BASE_TEST_DSP_ARCH@
BASE_TEST_TOOLCHAIN_FILE = @BASE_TEST_TOOLCHAIN_FILE@
BASE_TEST_CMAKE_EXTRA_ARGS = @BASE_TEST_CMAKE_EXTRA_ARGS@

# Ship test/base_test (including the vendor/unity submodule contents, when
# checked out) via a dist-hook instead of a $(shell find ...) EXTRA_DIST
# assignment.  The recipe runs under $(SHELL), avoiding GNU make's parse-time
# $(shell ...) extension.
dist-hook:
	@basetestdir=`cd '$(distdir)' && pwd`/test/base_test; \
	cd '$(BASE_TEST_SRCDIR)' && \
	find . -type f ! -path './builddir/*' ! -name '.git' | \
	while read -r f; do \
		$(MKDIR_P) "$$basetestdir/$$(dirname "$$f")"; \
		cp -p "$$f" "$$basetestdir/$$f"; \
	done

if BASE_TEST_ENABLED

.PHONY: base-test base-test-stage base-test-clean src/libcdsprpc.la

all-local: base-test

base-test: base-test-stage
	$(MKDIR_P) "$(BASE_TEST_BUILDDIR)"
	"$(BASE_TEST_CMAKE)" -S "$(BASE_TEST_SRCDIR)" \
		-B "$(BASE_TEST_BUILDDIR)" \
		-DCMAKE_TOOLCHAIN_FILE="$(BASE_TEST_TOOLCHAIN_FILE)" \
		$(BASE_TEST_CMAKE_EXTRA_ARGS) \
		-DFASTRPC_PREFIX="$(BASE_TEST_STAGEDIR)" \
		-UFASTRPC_LIBRARY -UFASTRPC_ROOT -UFASTRPC_INCLUDE_DIR \
		-DQAIC_EXECUTABLE="$(BASE_TEST_QAIC)" \
		-DHEXAGON_CLANG="$(BASE_TEST_HEXAGON_CLANG)" \
		-DHEXAGON_LD="$(BASE_TEST_HEXAGON_LD)" \
		-DDSP_ARCH="$(BASE_TEST_DSP_ARCH)" \
		-DCMAKE_C_COMPILER="$(CC)" \
		-DCMAKE_CXX_COMPILER="$(CXX)" \
		-DCMAKE_AR="$(AR)" \
		-DCMAKE_RANLIB="$(RANLIB)" \
		-DCMAKE_LINKER="$(LD)" \
		-DCMAKE_STRIP="$(STRIP)" \
		-DCMAKE_C_FLAGS="$(CPPFLAGS) $(CFLAGS)" \
		-DCMAKE_CXX_FLAGS="$(CPPFLAGS) $(CXXFLAGS)" \
		-DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)"
	"$(BASE_TEST_CMAKE)" --build "$(BASE_TEST_BUILDDIR)"

base-test-stage: src/libcdsprpc.la
	$(MKDIR_P) "$(BASE_TEST_STAGEDIR)"
	+$(MAKE) -C "$(abs_top_builddir)/inc" install-fastrpc_includeHEADERS \
		prefix="$(BASE_TEST_STAGEDIR)" exec_prefix="$(BASE_TEST_STAGEDIR)" \
		includedir="$(BASE_TEST_STAGEDIR)/include"
	+$(MAKE) -C "$(abs_top_builddir)/src" install-libLTLIBRARIES \
		lib_LTLIBRARIES=libcdsprpc.la \
		prefix="$(BASE_TEST_STAGEDIR)" exec_prefix="$(BASE_TEST_STAGEDIR)" \
		libdir="$(BASE_TEST_STAGEDIR)/lib"

# Recursive Automake builds do not expose individual subdirectory targets at
# the top level.  Delegate the library dependency to src/ explicitly so a
# direct or parallel `make base-test` cannot race CMake configuration.
src/libcdsprpc.la:
	+$(MAKE) -C "$(abs_top_builddir)/src" libcdsprpc.la

base-test-clean:
	$(AM_V_at)$(RM) -r "$(BASE_TEST_BUILDDIR)"

clean-local: base-test-clean
distclean-local: base-test-clean

endif

# The CMake project has its own dependency graph and must not be installed by
# the FastRPC library build.
