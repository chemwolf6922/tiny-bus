CFLAGS?=-O3
override CFLAGS+=-MMD -MP

LDFLAGS?=
override LDFLAGS+=-L..

PING_PONG_TEST_LIB=tbus tev
PING_PONG_TEST=ping_pong_test
PING_PONG_TEST_SRC=ping_pong_test.c

all:test

.PHONY:test
test:$(PING_PONG_TEST)

$(PING_PONG_TEST):$(patsubst %.c,%.o,$(PING_PONG_TEST_SRC))
	$(CC) $(LDFLAGS) -o $@ $^ $(patsubst %,-l%,$(PING_PONG_TEST_LIB))

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(wildcard *.d)

.PHONY:clean
clean:
	rm -f *.o *.d $(PING_PONG_TEST)

.PHONY:debug
debug:
	$(MAKE) CFLAGS="-g -O0"

