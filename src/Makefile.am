lib_LTLIBRARIES =

LIBDSPRPC_CFLAGS = -fno-short-enums -U_DEBUG -DARM_ARCH_7A -DLE_ENABLE -DNO_HAL -DENABLE_UPSTREAM_DRIVER_INTERFACE -DUSE_SYSLOG -I$(top_srcdir)/inc

LIBDSPRPC_SOURCES = \
		fastrpc_apps_user.c \
		fastrpc_perf.c \
		fastrpc_pm.c \
		fastrpc_config.c \
		fastrpc_async.c \
		fastrpc_mem.c \
		fastrpc_notif.c \
		fastrpc_latency.c \
		fastrpc_ioctl.c \
		fastrpc_log.c \
		fastrpc_procbuf.c \
		fastrpc_cap.c \
		log_config.c \
		dspsignal.c \
		dspqueue/dspqueue_cpu.c \
		dspqueue/dspqueue_rpc_stub.c \
		listener_android.c \
		apps_std_imp.c \
		apps_mem_imp.c \
		apps_mem_skel.c \
		rpcmem_linux.c \
		adspmsgd.c \
		adspmsgd_printf.c \
		std.c \
		std_path.c \
		std_mem.c \
		std_dtoa.c \
		std_strlprintf.c \
		BufBound.c \
		std_SwapBytes.c \
		smath.c \
		atomic.c \
		cae.c \
		platform_libs.c \
		pl_list.c \
		gpls.c \
		remotectl_stub.c \
		remotectl1_stub.c \
		adspmsgd_apps_skel.c \
		adspmsgd_adsp_stub.c \
		adspmsgd_adsp1_stub.c \
		apps_remotectl_skel.c \
		adsp_current_process_stub.c \
		adsp_current_process1_stub.c \
		adsp_listener_stub.c \
		adsp_listener1_stub.c \
		apps_std_skel.c \
		adsp_perf_stub.c \
		adsp_perf1_stub.c \
		mod_table.c \
		fastrpc_context.c

LIBDEFAULT_LISTENER_SOURCES = \
				adsp_default_listener.c \
				adsp_default_listener_stub.c \
				adsp_default_listener1_stub.c \
				std.c \
				std_mem.c

if ANDROID_CC
USE_LOG = -llog
endif

ADSP_CFLAGS = $(LIBDSPRPC_CFLAGS) -DDEFAULT_DOMAIN_ID=0

lib_LTLIBRARIES += libadsprpc.la
libadsprpc_la_SOURCES = $(LIBDSPRPC_SOURCES)
libadsprpc_la_LDFLAGS = -ldl -lm $(USE_LOG) -version-number @LT_VERSION_NUMBER@
libadsprpc_la_CFLAGS = $(ADSP_CFLAGS)

lib_LTLIBRARIES += libadsp_default_listener.la
libadsp_default_listener_la_SOURCES = $(LIBDEFAULT_LISTENER_SOURCES)
libadsp_default_listener_la_DEPENDENCIES = libadsprpc.la
libadsp_default_listener_la_LDFLAGS = libadsprpc.la -shared -module $(USE_LOG) -version-number @LT_VERSION_NUMBER@
libadsp_default_listener_la_CFLAGS = $(ADSP_CFLAGS) -DUSE_SYSLOG

CDSP_CFLAGS = $(LIBDSPRPC_CFLAGS) -DDEFAULT_DOMAIN_ID=3

lib_LTLIBRARIES += libcdsprpc.la
libcdsprpc_la_SOURCES = $(LIBDSPRPC_SOURCES)
libcdsprpc_la_LDFLAGS = -ldl -lm $(USE_LOG) -version-number @LT_VERSION_NUMBER@
libcdsprpc_la_CFLAGS = $(CDSP_CFLAGS)

lib_LTLIBRARIES += libcdsp_default_listener.la
libcdsp_default_listener_la_SOURCES = $(LIBDEFAULT_LISTENER_SOURCES)
libcdsp_default_listener_la_DEPENDENCIES = libcdsprpc.la
libcdsp_default_listener_la_LDFLAGS = libcdsprpc.la -shared -module $(USE_LOG) -version-number @LT_VERSION_NUMBER@
libcdsp_default_listener_la_CFLAGS = $(CDSP_CFLAGS) -DUSE_SYSLOG

SDSP_CFLAGS = $(LIBDSPRPC_CFLAGS) -DDEFAULT_DOMAIN_ID=2

lib_LTLIBRARIES += libsdsprpc.la
libsdsprpc_la_SOURCES = $(LIBDSPRPC_SOURCES)
libsdsprpc_la_LDFLAGS = -ldl -lm $(USE_LOG) -version-number @LT_VERSION_NUMBER@
libsdsprpc_la_CFLAGS = $(SDSP_CFLAGS)

lib_LTLIBRARIES += libsdsp_default_listener.la
libsdsp_default_listener_la_SOURCES = $(LIBDEFAULT_LISTENER_SOURCES)
libsdsp_default_listener_la_DEPENDENCIES = libsdsprpc.la
libsdsp_default_listener_la_LDFLAGS = libsdsprpc.la -shared -module $(USE_LOG) -version-number @LT_VERSION_NUMBER@
libsdsp_default_listener_la_CFLAGS = $(SDSP_CFLAGS) -DUSE_SYSLOG


bin_PROGRAMS = adsprpcd cdsprpcd sdsprpcd

adsprpcddir = $(libdir)
adsprpcd_SOURCES = adsprpcd.c
adsprpcd_DEPENDENCIES = libadsp_default_listener.la
adsprpcd_CFLAGS = -I$(top_srcdir)/inc -DDEFAULT_DOMAIN_ID=0 -DUSE_SYSLOG -DNO_HAL
adsprpcd_LDADD = -ldl $(USE_LOG)


cdsprpcddir = $(libdir)
cdsprpcd_SOURCES = cdsprpcd.c
cdsprpcd_DEPENDENCIES = libcdsp_default_listener.la
cdsprpcd_CFLAGS = -I$(top_srcdir)/inc -DDEFAULT_DOMAIN_ID=3 -DUSE_SYSLOG -DNO_HAL
cdsprpcd_LDADD =  -ldl $(USE_LOG)


sdsprpcddir = $(libdir)
sdsprpcd_SOURCES = sdsprpcd.c
sdsprpcd_DEPENDENCIES = libsdsp_default_listener.la
sdsprpcd_CFLAGS = -I$(top_srcdir)/inc -DDEFAULT_DOMAIN_ID=2 -DUSE_SYSLOG -DNO_HAL
sdsprpcd_LDADD =  -ldl $(USE_LOG)


# Export fastrpc headers
fastrpc_includedir       = $(includedir)/fastrpc
fastrpc_include_HEADERS  = $(top_srcdir)/inc/AEEStdErr.h
fastrpc_include_HEADERS += $(top_srcdir)/inc/remote.h
fastrpc_include_HEADERS += $(top_srcdir)/inc/rpcmem.h
