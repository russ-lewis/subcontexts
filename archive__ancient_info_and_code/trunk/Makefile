
CFLAGS=-fPIC -g -D_GNU_SOURCE

LIB_OBJS=subcontexts_core.o

HEADERS=$(wildcard *.h russ_common_code/c/*.h)



all: libsubcontexts.so subcontexts_init \
     subcontexts_demo.so     \
     subcontexts_dump        \
     subcontexts_ctl         \
     subcontexts_registry.so \
     subcontexts_ipc.so      \
     ipc_send ipc_recv



libsubcontexts.so: libsubcontexts.so.1
	ln -sf libsubcontexts.so.1 libsubcontexts.so

# use 'gcc' instead of 'ld' here - otherwise, we sometimes get link errors
# later, about "hidden symbols" being referenced.  I don't really
# understand it.
libsubcontexts.so.1: $(LIB_OBJS)
	$(CC) -shared $(LIB_OBJS) -o libsubcontexts.so.1

$(LIB_OBJS): $(HEADERS)

subcontexts_init: libsubcontexts.so subcontexts_init.c $(HEADERS)
	$(CC) $(CFLAGS) subcontexts_init.c -L. -lsubcontexts -ldl -g -o subcontexts_init



subcontexts_demo.so: subcontexts_demo.o
	$(CC) $(CFLAGS) -shared subcontexts_demo.o -o subcontexts_demo.so



# order of the arguments matter!  On "recent versions of gcc",
# the shared library which provides a symbol must come *AFTER* the
# file which imports them.  I guess that gcc is aggressively
# culling the symbol table???
#
# Ick!
#
# http://stackoverflow.com/questions/12748837/c-shared-library-undefined-reference-to-fooclasssayhello

subcontexts_dump: libsubcontexts.so subcontexts_dump.c $(HEADERS)
	$(CC) $(CFLAGS) subcontexts_dump.c -L. -lsubcontexts      -g -o subcontexts_dump

subcontexts_ctl: libsubcontexts.so subcontexts_ctl.c $(HEADERS)
	$(CC) $(CFLAGS) subcontexts_ctl.c -L. -lsubcontexts -g -o subcontexts_ctl



subcontexts_registry.so: subcontexts_registry.o
	$(CC) $(CFLAGS) -shared subcontexts_registry.o -o subcontexts_registry.so

subcontexts_ipc.so: subcontexts_ipc.o
	$(CC) $(CFLAGS) -shared subcontexts_ipc.o -o subcontexts_ipc.so



ipc_send: ipc_send.c $(HEADERS)
	$(CC) $(CFLAGS) ipc_send.c -L. -lsubcontexts -g -o ipc_send

ipc_recv: ipc_recv.c $(HEADERS)
	$(CC) $(CFLAGS) ipc_recv.c -L. -lsubcontexts -g -o ipc_recv



clean:
	-rm *.o *.so *.so.1 subcontexts_init subcontexts_dump subcontexts_ctl ipc_send ipc_recv

