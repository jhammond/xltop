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
LDFLAGS = -L/usr/local/lib -lev -lncurses

MAIN_OBJS = main.o ap_parse.o cl_listen.o clus.o hash.o host.o job.o k_heap.o \
            lnet.o n_buf.o screen.o serv.o sub.o x_node.o \
            botz.o evx_listen.o

OBJS = $(MAIN_OBJS) test_ap_parse.o

all: main qhost

main: $(MAIN_OBJS) /usr/local/lib/libconfuse.a

test_ap_parse: test_ap_parse.o ap_parse.o

# test_sub: test_sub.o x_node.o sub_node.o hash.o

-include $(OBJS:%.o=.%.d)

%.o: %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $*.c -o $*.o
	$(CC) -MM $(CFLAGS) $(CPPFLAGS) $*.c > .$*.d

.PHONY: clean
clean:
	rm -f main \
              test_ap_parse \
              test_sub \
              *.o
