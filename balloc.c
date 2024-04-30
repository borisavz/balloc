#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>

#define _GNU_SOURCE
#include <unistd.h>

typedef void * void_pointer;

typedef struct free_superblock_t free_superblock;
typedef struct used_superblock_t used_superblock;
typedef struct free_block_t free_block;
typedef struct private_heap_t private_heap;
typedef struct global_heap_t global_heap;

void binit();
void *balloc(size_t size);
void *balloc_size_class(size_t size);
void *balloc_mmap(size_t size);
int bfree(void *address);

global_heap *heap;

struct free_superblock_t {
    free_superblock *next;
};

struct used_superblock_t {
    int size_class;
    int high_water_mark;
    private_heap *owner;
};

struct free_block_t {
    free_block *next;
    used_superblock *parent;
};

struct private_heap_t {
    pthread_mutex_t lock;
    void_pointer free_list[4];
};

struct global_heap_t {
    free_superblock *free_superblock_list;
    private_heap private_heaps[4];
};

void binit() {
    heap = sbrk(sizeof(global_heap));

    int i;
    for(i = 0; i < 4; i++) {
        int j;
        for(j = 0; j < 4; j++) {
            heap->private_heaps[i].free_list[j] = NULL;
        }
    }
}

void *alloc_superblock() {
    return sbrk(sizeof(used_superblock) + 4096 * 16); /*why is 16 needed?*/
}

void *balloc(size_t size) {
    if(size <= 128) {
        return balloc_size_class(size);
    } else {
        return balloc_mmap(size);
    }
}

void *balloc_size_class(size_t size) {
    int size_class;
    int size_class_index;

    if(size <= 16) {
        size_class = 16;
        size_class_index = 0;
    } else if(size <= 32) {
        size_class = 32;
        size_class_index = 1;
    } else if(size <= 64) {
        size_class = 64;
        size_class_index = 2;
    } else if(size <= 128) {
        size_class = 128;
        size_class_index = 3;
    }

    int private_heap_index = gettid() % 4;
    
    private_heap *ph = &heap->private_heaps[private_heap_index];

    pthread_mutex_lock(&ph->lock);

    if(ph->free_list[size_class_index] == NULL) {
        used_superblock *sb = alloc_superblock();
        sb->size_class = size_class;
        sb->owner = ph;
        sb->high_water_mark = 0;

        free_block *first_free = sb + sizeof(used_superblock);
        /*first_free->next = first_free + size_class;*/
        first_free->next = NULL;
        first_free->parent = sb;

        ph->free_list[size_class_index] = first_free;
    }

    free_block *block = ph->free_list[size_class_index];

    if(block->next == NULL) {
        block->parent->high_water_mark += size_class;

        if(block->parent->high_water_mark < 4096) {
            free_block *next_free = block + size_class;
            next_free->next = NULL;
            next_free->parent = block->parent;

            ph->free_list[size_class_index] = next_free;
        } else {
            ph->free_list[size_class_index] = NULL;
        }
    } else {
        ph->free_list[size_class_index] = block->next;
    }
    
    pthread_mutex_unlock(&ph->lock);
    
    return block;
}

void *balloc_mmap(size_t size) {
    /*TODO: implement*/
    return NULL;
}

int bfree(void *address) {
    /*TODO: find how to know if block is from sbrk or mmap*/
}

int main() {
    binit();
    printf("%ld %d %d\n", heap, sizeof(void *), sizeof(short));
    
    int i;
    for(i = 0; i < 1000000; i++) {
        int *x;
        
        x = balloc(16);
        *x = 5;

        x = balloc(32);
        *x = 5;

        x = balloc(64);
        *x = 5;

        x = balloc(128);
        *x = 5;
    }

    return 0;
}