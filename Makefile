LIBTBUS_VERSION_MAJOR=1
LIBTBUS_VERSION_MINOR=0
LIBTBUS_VERSION=$(LIBTBUS_VERSION_MAJOR).$(LIBTBUS_VERSION_MINOR)

CFLAGS?=-O3
override CFLAGS+=-MMD -MP
ifneq ($(USE_SIGNAL), )
override CFLAGS+=-DUSE_SIGNAL
endif

LDFLAGS?=

DEPENDENCY_LIB=tev

STATIC_LIB=libtbus.a
SHARED_LIB=libtbus.so
LIB_SRC=client.c message.c message_reader.c message_writer.c

BROKER=tbus
BROKER_SRC=broker.c message.c message_reader.c topic_tree.c

TBUS_PUB=tbus_pub
TBUS_PUB_SRC=tbus_pub.c

TBUS_SUB=tbus_sub
TBUS_SUB_SRC=tbus_sub.c

ALL_SRC=$(LIB_SRC) $(BROKER_SRC) $(TBUS_PUB_SRC) $(TBUS_SUB_SRC)

all:broker lib tools

.PHONY:broker
broker:$(BROKER)

$(BROKER):$(patsubst %.c,%.o,$(BROKER_SRC))
	$(CC) $(LDFLAGS) -o $@ $^ $(patsubst %,-l%,$(DEPENDENCY_LIB))

.PHONY:lib
lib:$(STATIC_LIB) $(SHARED_LIB)

$(STATIC_LIB):$(patsubst %.c,%.o,$(LIB_SRC))
	$(AR) -rcs $@ *.o

$(SHARED_LIB):$(patsubst %.c,%.pic.o,$(LIB_SRC))
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$@.$(LIBTBUS_VERSION_MAJOR) -o $@.$(LIBTBUS_VERSION) $^ $(patsubst %,-l%,$(DEPENDENCY_LIB))
	ln -sf $@.$(LIBTBUS_VERSION) $@.$(LIBTBUS_VERSION_MAJOR)
	ln -sf $@.$(LIBTBUS_VERSION) $@

.PHONY:tools
tools:$(TBUS_PUB) $(TBUS_SUB)

$(TBUS_PUB):$(patsubst %.c,%.o,$(TBUS_PUB_SRC)) $(SHARED_LIB)
	$(CC) $(LDFLAGS) -L. $(patsubst lib%.so,-l%,$(SHARED_LIB)) -o $@ $(patsubst %.c,%.o,$(TBUS_PUB_SRC)) $(patsubst %,-l%,$(DEPENDENCY_LIB))

$(TBUS_SUB):$(patsubst %.c,%.o,$(TBUS_SUB_SRC)) $(SHARED_LIB)
	$(CC) $(LDFLAGS) -L. $(patsubst lib%.so,-l%,$(SHARED_LIB)) -o $@ $(patsubst %.c,%.o,$(TBUS_SUB_SRC)) $(patsubst %,-l%,$(DEPENDENCY_LIB))

%.pic.o:%.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(wildcard *.d)

clean:
	rm -f *.o *.d $(BROKER) $(TBUS_PUB) $(TBUS_SUB) $(STATIC_LIB) $(SHARED_LIB)*

install:lib broker tools
	install -d $(DESTDIR)/usr/lib
	install -m 644 $(STATIC_LIB) $(DESTDIR)/usr/lib
	install -m 644 $(SHARED_LIB).$(LIBTBUS_VERSION) $(DESTDIR)/usr/lib
	ln -sf $(DESTDIR)/usr/lib/$(SHARED_LIB).$(LIBTBUS_VERSION) $(DESTDIR)/usr/lib/$(SHARED_LIB).$(LIBTBUS_VERSION_MAJOR)
	ln -sf $(DESTDIR)/usr/lib/$(SHARED_LIB).$(LIBTBUS_VERSION) $(DESTDIR)/usr/lib/$(SHARED_LIB)
	install -d $(DESTDIR)/usr/include
	install -m 644 tbus.h $(DESTDIR)/usr/include
	install -d $(DESTDIR)/usr/bin
	install -m 755 $(BROKER) $(DESTDIR)/usr/bin
	install -m 755 $(TBUS_PUB) $(DESTDIR)/usr/bin
	install -m 755 $(TBUS_SUB) $(DESTDIR)/usr/bin

uninstall:
	rm -f $(DESTDIR)/usr/lib/$(STATIC_LIB)
	rm -f $(DESTDIR)/usr/lib/$(SHARED_LIB)*
	rm -f $(DESTDIR)/usr/include/tbus.h
	rm -f $(DESTDIR)/usr/bin/$(BROKER)
	rm -f $(DESTDIR)/usr/bin/$(TBUS_PUB)
	rm -f $(DESTDIR)/usr/bin/$(TBUS_SUB)
