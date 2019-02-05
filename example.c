#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tlsf.h"

#define TABLE_SIZE 1000000
#define ITERATIONS 10000000
#define MAX_SIZE 5000

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	size_t memory_size = 1000000000;
	char *memory = malloc(memory_size);
	char ctrl[tlsf_size()];
	int oom_count = 0;

	tlsf_t *tlsf = tlsf_create(ctrl);
	tlsf_pool_t *pool = tlsf_add_pool(tlsf, memory, memory_size);

	printf("tlsf_size()=%zu\n", tlsf_size());
	printf("tlsf_align_size()=%zu\n", tlsf_align_size());
	printf("tlsf_block_size_min()=%zu\n", tlsf_block_size_min());
	printf("tlsf_block_size_max()=%zu\n", tlsf_block_size_max());
	printf("tlsf_pool_overhead()=%zu\n", tlsf_pool_overhead());
	printf("tlsf_alloc_overhead()=%zu\n", tlsf_alloc_overhead());

	void *table[TABLE_SIZE] = { 0 };

	for (int i = 0; i < ITERATIONS; i++) {
		int j = rand() % TABLE_SIZE;
		size_t size = rand() % (MAX_SIZE + 1);
		if (table[j] != NULL) {
			memset(table[j], 0, tlsf_block_size(table[j]));
			tlsf_free(tlsf, table[j]);
		}
		void *ptr;
		if (i % 10 == 0) {
			int align_shift = 3 + rand() % 8;
			size_t align = (1<<align_shift);
			ptr = tlsf_memalign(tlsf, align, size);
		} else {
			ptr = tlsf_malloc(tlsf, size);
		}
		if (ptr == NULL) {
			oom_count++;
		} else {
			memset(ptr, 0, size);
		}
		table[j] = ptr;

	}
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (table[i] != NULL) {
			tlsf_free(tlsf, table[i]);
			table[i] = NULL;
		}
	}

	tlsf_remove_pool(tlsf, pool);

	tlsf_destroy(tlsf);

	memset(memory, 0, memory_size);
	printf("memory: %p\n", memory);
	printf("oom count: %d\n", oom_count);

	free(memory);
}
