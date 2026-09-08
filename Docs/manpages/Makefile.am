man8_MANS = dsprpcd.8
man3_MANS = fastrpc.3
man1_MANS = fastrpc-healthcheck.1

EXTRA_DIST = dsprpcd.8 fastrpc.3 fastrpc-healthcheck.1

install-data-hook:
	cd $(DESTDIR)$(mandir)/man8 && \
	ln -sf dsprpcd.8 adsprpcd.8 && \
	ln -sf dsprpcd.8 cdsprpcd.8 && \
	ln -sf dsprpcd.8 gdsprpcd.8 && \
	ln -sf dsprpcd.8 sdsprpcd.8

uninstall-hook:
	rm -f $(DESTDIR)$(mandir)/man8/adsprpcd.8
	rm -f $(DESTDIR)$(mandir)/man8/cdsprpcd.8
	rm -f $(DESTDIR)$(mandir)/man8/gdsprpcd.8
	rm -f $(DESTDIR)$(mandir)/man8/sdsprpcd.8
