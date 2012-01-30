DEBUG = 0

prefix = /usr/local
exec_prefix = ${prefix}
sysconfdir = /etc
bindir = ${exec_prefix}/bin

CLTOP_CONF_DIR = $(sysconfdir)/cltop
CLTOP_CONF_PATH = $(CLTOP_CONF_DIR)/cltop.conf
CLTOP_NIDS_PATH = $(CLTOP_CONF_DIR)/nids
CLTOP_RCMD_PATH = ${bindir}/cltop_rcmd

CC = gcc
CPPFLAGS = -D_GNU_SOURCE \
           -DDEBUG=$(DEBUG) \
           -DCLTOP_CONF_PATH=\"$(CLTOP_CONF_PATH)\" \
           -DCLTOP_NIDS_PATH=\"$(CLTOP_NIDS_PATH)\" \
           -DCLTOP_RCMD_PATH=\"$(CLTOP_RCMD_PATH)\" \
           -I/usr/local/include \
           -I ../confuse-2.7/src
CFLAGS = -Wall -Werror -g
LDFLAGS = -L/usr/local/lib -lev -lncurses

MAIN_OBJS = main.o ap_parse.o config.o n_buf.o cl_conn.o cl_bind.o hash.o \
            x_node.o

OBJS = $(MAIN_OBJS) rserv.o test_cl_conn.o test_ap_parse.o

main: $(MAIN_OBJS) /usr/local/lib/libconfuse.a

rserv: rserv.o
	$(CC) -lrt rserv.o -o $@

test_ap_parse: test_ap_parse.o ap_parse.o

test_cl_bind: test_cl_bind.o cl_bind.o cl_conn.o n_buf.o
	$(CC) $(LDFLAGS) test_cl_bind.o cl_bind.o cl_conn.o n_buf.o -o $@

test_cl_conn: test_cl_conn.o cl_conn.o n_buf.o
	$(CC) $(LDFLAGS) test_cl_conn.o cl_conn.o n_buf.o -o $@

test_n_buf: test_n_buf.o n_buf.o

test_sub: test_sub.o x_node.o sub_node.o hash.o

-include $(OBJS:%.o=.%.d)

%.o: %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $*.c -o $*.o
	$(CC) -MM $(CFLAGS) $(CPPFLAGS) $*.c > .$*.d

.PHONY: clean
clean:
	rm -f main rserv \
              test_ap_parse \
              test_cl_bind \
              test_cl_conn \
              test_n_buf \
              test_sub \
              *.o
