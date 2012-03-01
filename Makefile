DEBUG = 0

prefix = /usr/local
exec_prefix = ${prefix}
sysconfdir = ${prefix}/etc
bindir = ${exec_prefix}/bin

XLTOP_CONF_PATH = $(sysconfdir)/xltop.conf

CC = gcc
CPPFLAGS = -D_GNU_SOURCE \
           -DDEBUG=$(DEBUG) \
           -DXLTOP_CONF_PATH=\"$(XLTOP_CONF_PATH)\" \
           -I${prefix}/include

CFLAGS = -Wall -Werror -g
LDFLAGS = -L${prefix}/lib -lcurl -lev -lncurses

XLTOPD_OBJS = xltopd.o ap_parse.o clus.o fs.o hash.o host.o job.o k_heap.o \
            lnet.o n_buf.o screen.o serv.o sub.o x_node.o \
            query.o top.o x_botz.o \
            botz.o evx_listen.o

OBJS = $(XLTOPD_OBJS) # test_ap_parse.o

all: xltopd qhost servd xltop

xltopd: $(XLTOPD_OBJS) ${prefix}/lib/libconfuse.a
qhost: qhost.o
servd: servd.o hash.o n_buf.o
xltop: xltop.o hash.o n_buf.o screen.o

# test_ap_parse: test_ap_parse.o ap_parse.o
# test_sub: test_sub.o x_node.o sub_node.o hash.o

-include $(OBJS:%.o=.%.d)

.%.d: %.c
	$(CC) -MM $(CFLAGS) $(CPPFLAGS) $*.c > .$*.d

%.o: %.c .%.d
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $*.c -o $*.o

.PHONY: clean
clean:
	rm -f main \
              test_ap_parse \
              test_sub \
              *.o
