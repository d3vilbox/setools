lib_LIBRARIES = libapol.a
setoolsdir = @setoolsdir@

apolso_DATA = libapol.so.@libapol_version@
apolsodir = $(libdir)

AM_CFLAGS = @DEBUGCFLAGS@ @WARNCFLAGS@ @PROFILECFLAGS@ @SELINUX_CFLAGS@ \
	@QPOL_CFLAGS@ -fpic -I$(srcdir)/../include \
	-DAPOL_INSTALL_DIR='"${setoolsdir}"' \
	-DLIBAPOL_POLICY_INSTALL_DIR='"@selinux_policy_dir@/policy"' \
	-DLIBAPOL_DEFAULT_POLICY='"@selinux_default_policy@"'
AM_LDFLAGS = @DEBUGLDFLAGS@ @WARNLDFLAGS@ @PROFILELDFLAGS@

libapol_a_SOURCES = \
	avrule-query.c \
	bool-query.c \
	bounds-query.c \
	bst.c \
	class-perm-query.c \
	condrule-query.c \
	constraint-query.c \
	context-query.c \
	domain-trans-analysis.c domain-trans-analysis-internal.h \
	fscon-query.c \
	infoflow-analysis.c infoflow-analysis-internal.h \
	isid-query.c \
	mls-query.c \
	mls_level.c \
	mls_range.c \
	netcon-query.c \
	perm-map.c \
	permissive-query.c \
	polcap-query.c \
	policy.c \
	policy-path.c \
	policy-query.c \
	queue.c \
	range_trans-query.c \
	rbacrule-query.c \
	relabel-analysis.c \
	render.c \
	role-query.c \
	terule-query.c \
	ftrule-query.c \
	type-query.c \
	types-relation-analysis.c \
	user-query.c \
	util.c \
	vector.c vector-internal.h \
	policy-query-internal.h queue.h

libapol_a_DEPENDENCIES = $(top_builddir)/libqpol/src/libqpol.so

libapol_so_OBJS = $(patsubst %.c,%.o,$(filter %.c,$(libapol_a_SOURCES)))
LIBAPOL_SONAME = @libapol_soname@

dist_noinst_DATA = libapol.map

$(apolso_DATA): $(libapol_so_OBJS) libapol.map
	$(CC) -shared -o $@ $(libapol_so_OBJS) $(AM_LDFLAGS) $(LDFLAGS) -Wl,-soname,$(LIBAPOL_SONAME),--version-script=$(srcdir)/libapol.map,-z,defs $(top_builddir)/libqpol/src/libqpol.so
	$(LN_S) -f $@ @libapol_soname@
	$(LN_S) -f $@ libapol.so

libapol.so: $(apolso_DATA)

$(top_builddir)/libqpol/src/libqpol.so:
	$(MAKE) -C $(top_builddir)/libqpol/src $(notdir $@)

install-data-hook:
	cd $(DESTDIR)$(apolsodir) && $(LN_S) -f $(apolso_DATA) @libapol_soname@
	cd $(DESTDIR)$(apolsodir) && $(LN_S) -f $(apolso_DATA) libapol.so

mostlyclean-local:
	-rm -rf *.gcno *.gcda *.gprof *.gcov libapol.so @libapol_soname@ $(apolso_DATA)

uninstall-local:
	-rm -rf $(DESTDIR)$(apolsodir)/$(apolso_DATA) $(DESTDIR)$(apolsodir)/@libapol_soname@ $(DESTDIR)$(apolsodir)/libapol.so
