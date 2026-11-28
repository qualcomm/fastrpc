# Systemd service files

# Define systemd unit directory
systemdsystemunitdir = @systemdsystemunitdir@

# List of systemd service files to install (generated from .in templates).
#
# adsp/cdsp/cdsp1/gdsp0/gdsp1 keep their original, non-templated unit names
# (adsprpcd.service, cdsprpcd.service, cdsp1rpcd.service, gdsp0rpcd.service,
# gdsp1rpcd.service) so anything that enables/starts them by their exact
# legacy name keeps working.
#
# adsprpcd@.service/cdsprpcd@.service are systemd template
# units -- one file each, instantiated at runtime as e.g. "cdsprpcd@cdsp2.service"
# by the udev rule in 60-fastrpc.rules. They are only used for instances with
# no legacy name (adsp2+, cdsp2+). gdsp has no template unit -- only
# gdsp0/gdsp1 are supported, both covered by their legacy flat unit names.
systemdsystemunit_DATA = \
	adsprpcd.service \
	adsprpcd@.service \
	adsprpcd_audiopd.service \
	adsprpcd_audiopd@.service \
	cdsprpcd.service \
	cdsp1rpcd.service \
	cdsprpcd@.service \
	gdsp0rpcd.service \
	gdsp1rpcd.service \
	sdsprpcd.service

# Service template files
EXTRA_DIST = \
	adsprpcd.service.in \
	adsprpcd@.service.in \
	adsprpcd_audiopd.service.in \
	adsprpcd_audiopd@.service.in \
	cdsprpcd.service.in \
	cdsp1rpcd.service.in \
	cdsprpcd@.service.in \
	gdsp0rpcd.service.in \
	gdsp1rpcd.service.in \
	sdsprpcd.service.in

# Generate service files from templates with sed substitution
%.service: %.service.in Makefile
	$(AM_V_GEN)$(SED) \
		-e 's|@sbindir[@]|$(sbindir)|g' \
		$< > $@

# Clean generated service files
CLEANFILES = $(systemdsystemunit_DATA)

# sysusers.d file for creating fastrpc group
sysusersdir = @sysusersdir@
dist_sysusers_DATA = fastrpc.conf

# udev rules
udevrulesdir = @udevrulesdir@
dist_udevrules_DATA = 60-fastrpc.rules
