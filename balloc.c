#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/mman.h>

typedef void * void_pointer;

typedef struct large_object_t large_object;
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
void bfree(void *address);
void bfree_superblock(void *address);
void bfree_mmap(void *address);

global_heap *heap;
void *block_area_begin = NULL;
void *block_area_end = NULL;

struct large_object_t {
    size_t size;
};

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
    int shift_size = sizeof(used_superblock) + 4096 * 16; /*why is 16 needed?*/
    void *sb_addr = sbrk(shift_size);

    if(block_area_begin == NULL) {
        block_area_begin = sb_addr;
        block_area_end = sb_addr + shift_size;
    } else {
        block_area_end += shift_size;
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
    size_t total_size = sizeof(large_object) + size;
    
    large_object *obj = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    obj->size = sizeof(large_object);
    
    return obj + sizeof(large_object);
}

void bfree(void *address) {
    if(address >= block_area_begin && address <= block_area_end) {
        bfree_superblock(address);
    } else {
        bfree_mmap(address);
    }
}

void bfree_superblock(void *address) {
    free_block *new_free = (free_block *) address;
    used_superblock *sb = get_block_parent(address);

    pthread_mutex_lock(&sb->owner->lock);

    new_free->next = sb->owner->free_list[sb->size_class_index];
    sb->owner->free_list[sb->size_class_index] = new_free;

    pthread_mutex_unlock(&sb->owner->lock);
}

void bfree_mmap(void *address) {
    large_object *obj = address - sizeof(large_object);
    munmap(obj, obj->size + sizeof(large_object));
}

int main() {
    binit();
    
    int arr_len = 10000; /*too much mmaps are not possible, program fails*/

    int *arr16[5][arr_len];

    /*arr16 = balloc(5 * sizeof(int **));

    for(int i = 0; i < 5; i++) {
        arr16[i] = balloc(arr_len * sizeof(int *));
    }*/

    printf("begin alloc\n");
    for(int i = 0; i < arr_len; i++) {
        int *x;
        
        arr16[0][i] = balloc(16);
        *arr16[0][i] = 5;
        
        arr16[1][i] = balloc(32);
        *arr16[1][i] = 5;

        arr16[2][i] = balloc(64);
        *arr16[2][i] = 5;

        arr16[3][i] = balloc(128);
        *arr16[3][i] = 5;

        arr16[4][i] = balloc(256);
        *arr16[4][i] = 5;
    }

    printf("end alloc\n");
    printf("begin free\n");

    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < arr_len; j++) {
            bfree(arr16[i][j]);
        }
    }

    printf("end free\n");

    return 0;
}