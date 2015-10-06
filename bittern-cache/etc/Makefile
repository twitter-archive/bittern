all:
	echo Nothing to be done here.

install:
	install -d $(DESTDIR)/etc/init.d
	for file in init.d/bittern_cache*; do \
		install -m 0755 $$file $(DESTDIR)/etc/init.d/; \
	done

	install -d $(DESTDIR)/etc/bittern.conf.d/
	install -m 0644 bittern.conf $(DESTDIR)/etc/bittern.conf.d/bitcache0.conf.example
	install -d $(DESTDIR)/usr/share/bittern/addenum
	install -m 0644 rc.sysinit.patch $(DESTDIR)/usr/share/bittern/addenum
