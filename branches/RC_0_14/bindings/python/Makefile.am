if ENABLE_PYTHON_BINDING

all-local:
	$(PYTHON) setup.py build

install-exec-local:
	$(PYTHON) setup.py install --prefix=$(DESTDIR)$(prefix)

uninstall-local:
	rm -rf $(DESTDIR)$(libdir)/python*/site-packages/*libtorrentrb*

clean-local:
	$(PYTHON) setup.py clean --all

endif
