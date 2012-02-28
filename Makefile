DEBUG = 0

prefix = /usr/local
exec_prefix = ${prefix}
sysconfdir = /etc
bindir = ${exec_prefix}/bin

CLTOP_CONF_DIR = $(sysconfdir)/cltop
CLTOP_CONF_PATH = $(CLTOP_CONF_DIR)/cltop.conf

# -DCLTOP_CONF_PATH=\"$(CLTOP_CONF_PATH)\" \

CC = gcc
CPPFLAGS = -D_GNU_SOURCE \
           -DDEBUG=$(DEBUG) \
           -I/usr/local/include \
           -I../botz \
           -I../confuse-2.7/src

CFLAGS = -Wall -Werror -g
LDFLAGS = -L/usr/local/lib -lcurl -lev -lncurses

MAIN_OBJS = main.o ap_parse.o clus.o fs.o hash.o host.o job.o k_heap.o \
            lnet.o n_buf.o screen.o serv.o sub.o x_node.o \
            query.o top.o x_botz.o \
            botz.o evx_listen.o

OBJS = $(MAIN_OBJS) # test_ap_parse.o

all: main qhost servd xltop

main: $(MAIN_OBJS) /usr/local/lib/libconfuse.a
qhost: qhost.o
servd: servd.o hash.o n_buf.o
xltop: xltop.o hash.o n_buf.o

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
