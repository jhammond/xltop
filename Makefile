DEBUG = 0

name = xltop
prefix = /usr/local
exec_prefix = ${prefix}
sysconfdir = ${prefix}/etc
bindir = ${exec_prefix}/bin

XLTOP_CONF_PATH = ${sysconfdir}/${name}.conf

CC = gcc
CFLAGS = -Wall -Werror -g
CPPFLAGS = -D_GNU_SOURCE \
           -DDEBUG=${DEBUG} \
           -DXLTOP_CONF_PATH=\"${XLTOP_CONF_PATH}\" \
           -I${prefix}/include \
           -I./botz
LDFLAGS = -L${prefix}/lib -lev

XLTOP_OBJS = xltop.o hash.o n_buf.o screen.o curl_x.o
CLUSD_OBJS = clusd.o curl_x.o n_buf.o 
SERVD_OBJS = servd.o hash.o n_buf.o
MASTER_OBJS = master.o ap_parse.o clus.o fs.o hash.o host.o job.o k_heap.o \
              lnet.o n_buf.o screen.o serv.o sub.o x_node.o \
              query.o top.o x_botz.o botz.o evx_listen.o

OBJS = ${XLTOP_OBJS} ${CLUSD_OBJS} ${SERVD_OBJS} ${MASTER_OBJS}

all: ${name} ${name}-clusd ${name}-master ${name}-servd

${name}: ${XLTOP_OBJS}
	${CC} ${LDFLAGS} -o $@ ${XLTOP_OBJS} -lcurl -lncurses

${name}-clusd: ${CLUSD_OBJS}
	${CC} ${LDFLAGS} -o $@ ${CLUSD_OBJS} -lcurl

${name}-master: ${MASTER_OBJS}
	${CC} ${LDFLAGS} -o $@ ${MASTER_OBJS} -lncurses ${prefix}/lib/libconfuse.a

${name}-servd: ${SERVD_OBJS}
	${CC} ${LDFLAGS} -o $@ ${SERVD_OBJS} -lcurl

-include $(OBJS:%.o=.%.d)

.%.d: %.c
	${CC} -MM ${CFLAGS} ${CPPFLAGS} $*.c > .$*.d

%.o: %.c .%.d
	${CC} -c ${CFLAGS} ${CPPFLAGS} $*.c -o $*.o

.PHONY: clean
clean:
	rm -f ${name} ${name}-clusd ${name}-master ${name}-servd *.o
