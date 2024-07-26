CFLAGS?=-O3
override CFLAGS+=-MMD -MP

LDFLAGS?=
override LDFLAGS+=-L..

.PHONY:all
all:test

PING_PONG_TEST=ping_pong_test
PING_PONG_TEST_SRC=ping_pong_test.c
PING_PONG_TEST_LIB=tbus tev
$(PING_PONG_TEST):$(patsubst %.c,%.o,$(PING_PONG_TEST_SRC))
	$(CC) $(LDFLAGS) -o $@ $^ $(patsubst %,-l%,$(PING_PONG_TEST_LIB))

MESSAGE_TEST=message_test
MESSAGE_TEST_SRC=message_test.c ../message.c
MESSAGE_TEST_LIB=
$(MESSAGE_TEST):$(patsubst %.c,%.o,$(MESSAGE_TEST_SRC))
	$(CC) $(LDFLAGS) -o $@ $^ $(patsubst %,-l%,$(MESSAGE_TEST_LIB))

TOPIC_TREE_TEST=topic_tree_test
TOPIC_TREE_TEST_SRC=topic_tree_test.c ../topic_tree.c
TOPIC_TREE_TEST_LIB=tev
$(TOPIC_TREE_TEST):$(patsubst %.c,%.o,$(TOPIC_TREE_TEST_SRC))
	$(CC) $(LDFLAGS) -o $@ $^ $(patsubst %,-l%,$(TOPIC_TREE_TEST_LIB))

LARGE_MESSAGE_TEST=large_message_test
LARGE_MESSAGE_TEST_SRC=large_message_test.c
LARGE_MESSAGE_TEST_LIB=tbus tev
$(LARGE_MESSAGE_TEST):$(patsubst %.c,%.o,$(LARGE_MESSAGE_TEST_SRC))
	$(CC) $(LDFLAGS) -o $@ $^ $(patsubst %,-l%,$(LARGE_MESSAGE_TEST_LIB))

ALL_TESTS=$(PING_PONG_TEST) $(MESSAGE_TEST) $(TOPIC_TREE_TEST) $(LARGE_MESSAGE_TEST)

.PHONY:test
test:$(ALL_TESTS)

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(wildcard *.d)

.PHONY:clean
clean:
	rm -f *.o *.d $(ALL_TESTS)

.PHONY:debug
debug:
	$(MAKE) CFLAGS="-g -O0"

