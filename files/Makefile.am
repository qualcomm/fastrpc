# Systemd service files

# Define systemd unit directory
systemdsystemunitdir = @systemdsystemunitdir@

# List of systemd service files to install (generated from .in templates)
systemdsystemunit_DATA = \
	adsprpcd.service \
	adsprpcd_audiopd.service \
	cdsprpcd.service \
	cdsp1rpcd.service \
	gdsp0rpcd.service \
	gdsp1rpcd.service \
	sdsprpcd.service

# Service template files
EXTRA_DIST = \
	adsprpcd.service.in \
	adsprpcd_audiopd.service.in \
	cdsprpcd.service.in \
	cdsp1rpcd.service.in \
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
dist_udevrules_DATA = \
	59-fastrpc-remoteproc.rules \
	60-fastrpc.rules
