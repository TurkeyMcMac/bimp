#include <stddef.h>
#include <string.h>

/** PROTOTYPES FOR PLATFORM-SPECIFIC STUFF **/

static char *start, *end;
static void lock(void);
static void unlock(void);
static int needs_init(void);
static void init_heap(void);
static int request_more(char *new_end);
static void set_enomem(void);

/** PORTABLE INTERNALS **/

#define ASSERT(name, cond) typedef int assert_##name[((cond) != 0) * 2 - 1]

#if defined(__GNUC__)
#	define EXPECT(cond, expect) __builtin_expect(cond, expect)
#else
#	define EXPECT(cond, expect) (cond)
#endif
#define LIKELY(cond) EXPECT((cond) != 0, 1)
#define UNLIKELY(cond) EXPECT((cond) != 0, 0)

#define IS_POW_2(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)

#if !defined(BIMP_ALIGN)
#	define BIMP_ALIGN 16
#endif
union header {
	struct header_s {
		size_t size;
		size_t back;
	} s;
	char pad[BIMP_ALIGN];
};
#define ALIGN sizeof(union header)
ASSERT(alignment_works, ALIGN % BIMP_ALIGN == 0);
#define ROUND_SIZE(size, align) (((size) + align - 1) / align * align)
#define GET_HEADER(mem) \
	((struct header_s *)((char *)(mem) - sizeof(union header)))
#define IS_FREED(mem) ((GET_HEADER(mem)->back & 1) == 0)
#define IN_USE_BACK(back) ((back) | 1)
#define FREED_BACK(back) ((back) & ~1)

static char *top_mem;

static void init_once(void)
{
	if (UNLIKELY(needs_init())) {
		init_heap();
		top_mem = start + sizeof(union header);
		GET_HEADER(top_mem)->back = IN_USE_BACK(0);
		GET_HEADER(top_mem)->size = 0;
	}
}

static void *malloc_no_lock(size_t size)
{
	size_t back;
	char *new_top;
	init_once();
	back = sizeof(union header) + GET_HEADER(top_mem)->size;
	new_top = top_mem + back;
	size = ROUND_SIZE(size, ALIGN);
	if (UNLIKELY(request_more(new_top + size))) {
		set_enomem();
		return NULL;
	}
	GET_HEADER(new_top)->back = IN_USE_BACK(back);
	GET_HEADER(new_top)->size = size;
	top_mem = new_top;
	return new_top;
}

static void free_no_lock_nonnull(void *mem)
{
	if (mem == top_mem) {
		top_mem -= FREED_BACK(GET_HEADER(top_mem)->back);
		/* Loop in case back was too large to store in one size_t: */
		while (IS_FREED(top_mem)) {
			top_mem -= GET_HEADER(top_mem)->back;
		}
	} else {
		size_t freed_back = FREED_BACK(GET_HEADER(mem)->back);
		void *back_mem = (char *)mem - freed_back;
		if (IS_FREED(back_mem)) {
			size_t already_freed = GET_HEADER(back_mem)->back;
			size_t total_freed = freed_back + already_freed;
			/* The if protects against overflow: */
			if (total_freed >= freed_back) freed_back = total_freed;
		}
		GET_HEADER(mem)->back = freed_back;
	}
}

void *malloc(size_t size);
static void *malloc_aligned(size_t align, size_t size)
{
	size_t old_top_size;
	size_t align_offset;
	void *mem;
	if (align <= ALIGN) return malloc(size);
	lock();
	init_once();
	old_top_size = GET_HEADER(top_mem)->size;
	mem = top_mem + old_top_size;
	align_offset = (size_t)(mem + sizeof(union header)) & (align - 1);
	if (align_offset != 0) mem += align - align_offset;
	GET_HEADER(top_mem)->size = (char *)mem - top_mem;
	if (LIKELY(GET_HEADER(top_mem)->size >= old_top_size)) {
		mem = malloc_no_lock(size);
	} else {
		/* Just in case the size shrinks due to overflow somewhere. */
		mem = NULL;
		set_enomem();
	}
	if (UNLIKELY(!mem)) GET_HEADER(top_mem)->size = old_top_size;
	unlock();
	return mem;
}

/** PORTABLE INTERFACE **/

void *malloc(size_t size)
{
	void *mem;
	lock();
	mem = malloc_no_lock(size);
	unlock();
	return mem;
}

void *calloc(size_t nmemb, size_t size)
{
	void *mem;
	size_t total = nmemb * size;
	if (nmemb > 1 && UNLIKELY(total / nmemb != size)) {
		set_enomem();
		return NULL;
	}
	mem = malloc(total);
	if (UNLIKELY(!mem)) return NULL;
	return memset(mem, 0, total);
}

void *realloc(void *mem, size_t size)
{
	size_t *old_size;
	if (!mem) return malloc(size);
	lock();
	old_size = &GET_HEADER(mem)->size;
	if (size <= *old_size) {
		*old_size = ROUND_SIZE(size, ALIGN);
	} else if (mem == top_mem) {
		size = ROUND_SIZE(size, ALIGN);
		if (UNLIKELY(request_more(top_mem + size))) {
			set_enomem();
			mem = NULL;
		} else {
			*old_size = size;
		}
	} else {
		void *old_mem = mem;
		mem = malloc_no_lock(size);
		if (LIKELY(mem)) {
			memcpy(mem, old_mem, *old_size);
			free_no_lock_nonnull(old_mem);
		}
	}
	unlock();
	return mem;
}

void free(void *mem)
{
	if (!mem) return;
	lock();
	free_no_lock_nonnull(mem);
	unlock();
}

void *aligned_alloc(size_t align, size_t size)
{
	return malloc_aligned(align, size);
}

size_t malloc_usable_size(void *mem)
{
	size_t size;
	if (!mem) return 0;
	lock();
	size = GET_HEADER(mem)->size;
	unlock();
	return size;
}

void *memalign(size_t align, size_t size)
{
	return malloc_aligned(align, size);
}

void *reallocarray(void *mem, size_t nmemb, size_t size)
{
	size_t total = nmemb * size;
	if (nmemb > 1 && UNLIKELY(total / nmemb != size)) {
		set_enomem();
		return NULL;
	}
	return realloc(mem, total);
}

void *reallocf(void *mem, size_t size)
{
	void *new_mem = realloc(mem, size);
	if (!new_mem) free(mem);
	return new_mem;
}

#if defined(__unix__)

/* posix_memalign, valloc, and pvalloc are Unix-only because posix_memalign
 * requires errno for its return values and valloc and pvalloc require sysconf
 * to find the page size. */

#	include <errno.h>
#	include <unistd.h>

int posix_memalign(void **memptr, size_t align, size_t size)
{
	int errnum;
	void *mem;
	if (align % sizeof(void *) != 0 || !IS_POW_2(align)) return EINVAL;
	errnum = errno;
	mem = malloc_aligned(align, size);
	if (UNLIKELY(!mem)) {
		errno = errnum;
		return ENOMEM;
	}
	*memptr = mem;
	return 0;
}

void *valloc(size_t size)
{
	return malloc_aligned(sysconf(_SC_PAGESIZE), size);
}

void *pvalloc(size_t size)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	return malloc_aligned(pagesize, ROUND_SIZE(size, pagesize));
}

#endif /* defined(__unix__) */

/** ARCHITECTURE-SPECIFIC IMPLEMENTATION **/

#if defined(BIMP_SINGLE_THREADED)

static void lock(void)
{
}

static void unlock(void)
{
}

#endif /* defined(BIMP_SINGLE_THREADED) */

#if defined(BIMP_SET_ERRNO) || defined(__unix__)

#	include <errno.h>

static void set_enomem(void)
{
	errno = ENOMEM;
}

#endif /* defined(BIMP_SET_ERRNO) || defined(__unix__) */

#if defined(__linux__)
#	include <sys/syscall.h>
#	include <unistd.h>

#	if !defined(BIMP_SINGLE_THREADED)

#		include <linux/futex.h>
#		include <stdint.h>

/* lock and unlock based on https://akkadia.org/drepper/futex.pdf page 8. */

static uint32_t cmpxchg(volatile uint32_t *val, uint32_t expected, uint32_t desired)
{
	__atomic_compare_exchange_n(val, &expected, desired, 0,
		__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return expected;
}

static uint32_t xchg(volatile uint32_t *val, uint32_t put)
{
	return __atomic_exchange_n(val, put, __ATOMIC_SEQ_CST);
}

static uint32_t fetch_sub(volatile uint32_t *val, uint32_t sub)
{
	return __atomic_fetch_sub(val, sub, __ATOMIC_SEQ_CST);
}

static void store(volatile uint32_t *val, uint32_t put)
{
	__atomic_store_n(val, put, __ATOMIC_SEQ_CST);
}

static volatile uint32_t futex = 0;

static void lock(void)
{
	uint32_t c;
	if ((c = cmpxchg(&futex, 0, 1)) != 0) {
		if (c != 2) c = xchg(&futex, 2);
		while (c != 0) {
			int errnum = errno;
			syscall(SYS_futex, &futex, FUTEX_WAIT_PRIVATE,
				2, NULL, NULL, 0);
			errno = errnum;
			c = xchg(&futex, 2);
		}
	}
}

static void unlock(void)
{
	if (fetch_sub(&futex, 1) != 1) {
		int errnum = errno;
		store(&futex, 0);
		syscall(SYS_futex, &futex, FUTEX_WAKE_PRIVATE,
			1, NULL, NULL, 0);
		errno = errnum;
	}
}

#	endif /* !defined(BIMP_SINGLE_THREADED) */

static int needs_init(void)
{
	return UNLIKELY(!start);
}

static void init_heap(void)
{
	int errnum = errno;
	start = (void *)syscall(SYS_brk, NULL);
	end = (void *)syscall(SYS_brk, start + 1024);
	errno = errnum;
}

static int request_more(char *new_end)
{
	ptrdiff_t size;
	int errnum;
	if (new_end <= end) return 0;
	new_end += (new_end - start) / 2;
	errnum = errno;
	if (LIKELY((char *)syscall(SYS_brk, new_end) == new_end)) {
		end = new_end;
		return 0;
	} else {
		errno = errnum;
		return -1;
	}
}

/* endif defined(__linux__) */
#else
#	error Unsupported platform.
#endif
