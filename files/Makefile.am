# Systemd service files

# Define systemd unit directory
systemdsystemunitdir = @systemdsystemunitdir@

# List of systemd service files to install (generated from .in templates).
# adsprpcd@.service/cdsprpcd@.service/gdsprpcd@.service are systemd template
# units -- one file each, instantiated at runtime as e.g. "cdsprpcd@cdsp2.service"
# by the udev rule in 60-fastrpc.rules. A new instance of an already-supported
# family (another cdspN/adspN/gdspN) needs no new file here.
systemdsystemunit_DATA = \
	adsprpcd@.service \
	adsprpcd_audiopd.service \
	cdsprpcd@.service \
	gdsprpcd@.service \
	sdsprpcd.service

# Service template files
EXTRA_DIST = \
	adsprpcd@.service.in \
	adsprpcd_audiopd.service.in \
	cdsprpcd@.service.in \
	gdsprpcd@.service.in \
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
