target-linux = libbimp.so
target-apple = libbimp.dylib

c-flags = -O3 -flto -shared -fPIC -fno-builtin-malloc -fno-builtin-calloc \
	-fno-builtin-realloc -fno-builtin-free -Wall -Wextra $(CFLAGS)

ifeq ($(shell uname -s), Linux)
all: $(target-linux)
endif

ifeq ($(shell uname -s), Darwin)
all: $(target-apple)
endif

$(target-linux): bimp.c
	$(CC) -D_GNU_SOURCE -std=gnu99 $(c-flags) -o $@ $<

$(target-apple): bimp.c
	@# -Wno-deprecated-declarations is for brk and sbrk. I probably
	@# shouldn't be using them, but they map well to my use case.
	$(CC) -D_GNU_SOURCE -std=gnu99 -Wno-deprecated-declarations $(c-flags) \
		-o $@ $<

clean:
	$(RM) $(target-linux) $(target-apple)
