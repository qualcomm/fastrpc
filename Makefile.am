SUBDIRS = inc src test files Docs/manpages

ACLOCAL_AMFLAGS = -I m4

COVERAGE_INFO = fastrpc-coverage.info
COVERAGE_REPORT_DIR = fastrpc-coverage

if CODE_COVERAGE_ENABLED
coverage: all
	$(LCOV) --capture --initial \
		--directory "$(abs_top_builddir)/src" \
		--base-directory "$(abs_top_srcdir)/src" \
		--gcov-tool "$(GCOV)" \
		--output-file "$(COVERAGE_INFO).base"
	$(LCOV) --capture \
		--directory "$(abs_top_builddir)/src" \
		--base-directory "$(abs_top_srcdir)/src" \
		--gcov-tool "$(GCOV)" \
		--output-file "$(COVERAGE_INFO).run"
	$(LCOV) --add-tracefile "$(COVERAGE_INFO).base" \
		--add-tracefile "$(COVERAGE_INFO).run" \
		--output-file "$(COVERAGE_INFO).tmp"
	$(LCOV) --extract "$(COVERAGE_INFO).tmp" '*/src/*' \
		--output-file "$(COVERAGE_INFO)"
	$(GENHTML) "$(COVERAGE_INFO)" \
		--output-directory "$(COVERAGE_REPORT_DIR)" \
		--title "FastRPC code coverage" \
		--legend --show-details
	$(RM) "$(COVERAGE_INFO).base" "$(COVERAGE_INFO).run" \
		"$(COVERAGE_INFO).tmp"
	@echo "Coverage report: $(abs_top_builddir)/$(COVERAGE_REPORT_DIR)/index.html"
else
coverage:
	@echo "Reconfigure with --enable-code-coverage to generate a coverage report."
	@false
endif

coverage-clean:
	-find "$(abs_top_builddir)" -name '*.gcda' -delete
	$(RM) "$(COVERAGE_INFO)" "$(COVERAGE_INFO).base" \
		"$(COVERAGE_INFO).run" "$(COVERAGE_INFO).tmp"
	$(RM) -r "$(COVERAGE_REPORT_DIR)"

clean-local: coverage-clean

.PHONY: coverage coverage-clean
