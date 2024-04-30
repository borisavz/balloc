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
used_superblock *get_block_parent(void *block_address);
void *balloc_mmap(size_t size);
int bfree(void *address);

global_heap *heap;
void *block_area_begin = NULL;

struct free_superblock_t {
    free_superblock *next;
};

struct used_superblock_t {
    int size_class_index;
    int high_water_mark;
    private_heap *owner;
};

struct free_block_t {
    free_block *next;
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

    for(int i = 0; i < 4; i++) {
        for(int j = 0; j < 4; j++) {
            heap->private_heaps[i].free_list[j] = NULL;
        }
    }
}

void *alloc_superblock() {
    void *sb_addr = sbrk(sizeof(used_superblock) + 4096 * 16); /*why is 16 needed?*/

    if(block_area_begin == NULL) {
        block_area_begin = sb_addr;
    }

    return sb_addr;
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
        printf("sb 1 %ld\n", sb);
        sb->size_class_index = size_class_index;
        sb->owner = ph;
        sb->high_water_mark = 0;

        free_block *first_free = sb + sizeof(used_superblock);
        first_free->next = NULL;

        ph->free_list[size_class_index] = first_free;
    }

    free_block *block = ph->free_list[size_class_index];

    if(block->next == NULL) {
        used_superblock *block_parent = get_block_parent(block);
        printf("sb 2 %ld\n", block_parent);
        block_parent->high_water_mark += size_class;

        if(block_parent->high_water_mark < 4096) {
            free_block *next_free = block + size_class;
            next_free->next = NULL;

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

used_superblock *get_block_parent(void *block_address) {
    void *addr_diff = (void *) block_address - (void *) block_area_begin;

    long unsigned int sb_index = (long unsigned int) addr_diff / (sizeof(used_superblock) + 4096 * 16);

    return block_area_begin + sb_index * (sizeof(used_superblock) + 4096 * 16);
}

void *balloc_mmap(size_t size) {
    /*TODO: implement*/
    return NULL;
}

int bfree(void *address) {
    /*TODO: find how to know if block is from sbrk or mmap*/
    /*if heap is obtained using only sbrk, then range check is possible*/
    
    free_block *new_free = (free_block *) address;
    used_superblock *sb = get_block_parent(address);

    pthread_mutex_lock(&sb->owner->lock);

    new_free->next = sb->owner->free_list[sb->size_class_index];
    sb->owner->free_list[sb->size_class_index] = new_free;

    pthread_mutex_unlock(&sb->owner->lock);
}

int main() {
    binit();
    printf("%ld %d %d\n", heap, sizeof(void *), sizeof(short));
    
    int *arr16[4][100000];

    for(int i = 0; i < 100000; i++) {
        int *x;
        
        arr16[0][i] = balloc(16);
        *arr16[0][i] = 5;
        
        arr16[1][i] = balloc(32);
        *arr16[1][i] = 5;

        arr16[2][i] = balloc(64);
        *arr16[2][i] = 5;

        arr16[3][i] = balloc(128);
        *arr16[3][i] = 5;
    }

    for(int i = 0; i < 4; i++) {
        for(int j = 0; j < 100000; j++) {
            bfree(arr16[i][j]);
        }
    }

    return 0;
}