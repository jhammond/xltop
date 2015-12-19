AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DXLTOP_CONF_DIR=\"${XLTOP_CONF_DIR}\" \
	-DXLTOP_MASTER=\"${XLTOP_MASTER}\" \
	-DXLTOP_PORT=\"${XLTOP_PORT}\" \
	-DXLTOP_CLUS=\"${XLTOP_CLUS}\" \
	-DXLTOP_DOMAIN=\"${XLTOP_DOMAIN}\" \
	-I/usr/include/libev \
	-I${top_srcdir}/src

AM_CFLAGS = -g -Wall -Werror -Wno-strict-aliasing

bin_PROGRAMS = xltop xltop-clusd xltop-master xltop-servd

xltop_SOURCES = xltop.c hash.c n_buf.c screen.c curl_x.c

xltop_LDADD = -lcurl -lev -lncurses -lm

xltop_clusd_SOURCES = clusd.c curl_x.c n_buf.c

xltop_clusd_LDADD = -lcurl -lev -lm

xltop_master_SOURCES = \
	master.c ap_parse.c hash.c x_node.c sub.c \
	lnet.c host.c job.c clus.c serv.c fs.c \
	k_heap.c top.c query.c \
	n_buf.c evx_listen.c x_botz.c botz.c \
	pidfile.c

xltop_master_LDADD = -lconfuse -lev -lncurses -lm

xltop_servd_SOURCES = servd.c curl_x.c hash.c n_buf.c pidfile.c

xltop_servd_LDADD = -lcurl -lev -lm
