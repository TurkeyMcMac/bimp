target-linux = bimp-linux.so

$(target-linux): bimp.c
	$(CC)	-o $@ $< -O3 -flto -shared -fPIC -D_GNU_SOURCE -std=gnu99 \
		-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc \
		-fno-builtin-free
	
