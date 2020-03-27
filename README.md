# Bimp: Improved Bump Allocator

This is a memory allocator implementing `malloc`, `free`, etc. It works by
simply appending newly allocated memory to the end of the segment of already
allocated regions, as with any bump allocator. The improvement is that memory
can be freed from the top of the allocation stack when it forms a continuous
segment. When the most recent allocation is freed, it will also free the memory
immediately behind it that is marked as free.

Say you had a function like this:

```
void process( /* Some arguments */ )
{
	char *a = malloc(100);
	char *b = malloc(200);

	/* Do some work. */

	free(a);
	free(b);
}
```

Using Bimp, memory would not leak, since `a` and `b` would be freed while they
were still at the top of the memory stack.

## Usage

Compile the shared library like so:

```
make
```

You can then link it with pre-existing programs.

Linux:

```
LD_PRELOAD=./libbimp.so my-program
```

Mac:

```
DYLD_INSERT_LIBRARIES=./libbimp.dylib my-program
```

### Compilation Options

Some symbols can be defined to affect compilation. They are as follows:

* `BIMP_ALIGN`: The numeric alignment of allocations. It must be a power of two.
* `BIMP_SET_ERRNO`: Force errno to be set. It will always be set on Unix.
* `BIMP_SINGLE_THREADED`: Assume usage by a single thread. This removes the
  overhead of locking.

Define them like so:

```
make CFLAGS='-DBIMP_ALIGN=32 -DBIMP_SET_ERRNO ...'
```

## Implemented functions

The following are implemented in `bimp.c`:

* `malloc`
* `calloc`
* `realloc`
* `free`
* `aligned_alloc`
* `malloc_usable_size`
* `reallocarray`
* `reallocf`
* `memalign`
* `posix_memalign`
* `valloc`
* `pvalloc`

## Supported platforms

Currently, implementations only exist for Linux and Mac. They both use `brk` to
allocate memory, which is not optimal. Not much platfrom-specific code needs to
be written to port the allocator to a new platform. Most of the code is portable
and conforms to ANSI C.

## Usefulness

Do not use this allocator. Despite being simple, it is somewhat slower than the
system allocator according to my primitive test on Linux. I do not know if the
slowness is due to the architecture of the project or just the implementation.
