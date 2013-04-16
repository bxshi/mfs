
#ifndef _MFS_HASH_H_
#define _MFS_HASH_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include "uthash.h"

#define MFS_BLOCK_PER_BLKMAP 1024
#define MFS_HASH_POOL_MAX 1024

#define mfs_hash_attr stat

struct mfs_hash_block {
	char data[4096];
	size_t size;
	struct mfs_hash_block *next;
	struct mfs_hash_block *prev;
	struct mfs_hash_element *e;
	int index;
};

struct mfs_hash_blkmap {
	struct mfs_hash_block *block[MFS_BLOCK_PER_BLKMAP];/*no need to store*/
	short type[MFS_BLOCK_PER_BLKMAP];
	int next;

};

struct mfs_hash_serv_element {
	UT_hash_handle hh;
	size_t size;
	size_t st_size;
	char *data;
	pthread_mutex_t t_lock;
	int fd;
	char path[];
};

struct mfs_hash_element {
	UT_hash_handle hh;
	struct stat attr;
	int attr_err;
	int* blkmap_addr; /* 0 no 1 cache 2 disk */
	int* blkmap_size; /* size of each block */
	char* blkmap_continue; /* continue or not */
	char **blkmap_mem;/* addr of blkmap_cache */
	int *blkmap_offset;
	void **blkmap_LRU;/* addr of LRU */
	pthread_rwlock_t lock;//does this need reinitialize after load?
	struct mfs_hash_dir_entry *dir_entry;
	int dir_err;
	void *ra;
	char *symbol_link;
	char path[];
};

struct mfs_hash_dir_entry {
	ino_t st_ino;
	mode_t st_mode;
	char d_name[256];
	struct mfs_hash_dir_entry *next;
};

struct mfs_hash_element_dir {
	UT_hash_handle hh;
	struct mfs_hash_element_dir *entry;
	char path[];
};

struct mfs_hash_pool_element {//abondoned
	void *addr;
};

struct mfs_hash_pool_container {

	int usable_attr;
	int p_attr;
	struct mfs_hash_pool_element *attr[MFS_HASH_POOL_MAX];
	
	int usable_blkmap;
	int p_blkmap;
	struct mfs_hash_pool_element *blkmap[MFS_HASH_POOL_MAX];
	
	struct mfs_hash_block *block;
	struct mfs_hash_block *LRU_list;
	struct mfs_hash_block *LRU_list_end;
	int block_cnt;
	
};

/*global var*/
struct mfs_hash_pool_container *mfs_hash_pool_stat_p;
static struct mfs_hash_element *mfs_hash_table;
static struct mfs_hash_serv_element *mfs_hash_serv_table;
sem_t *mfs_hash_pool_attr_sem;
sem_t *mfs_hash_pool_blkmap_sem;
sem_t *mfs_hash_pool_block_sem;
sem_t *mfs_hash_pool_usable_attr_sem;
sem_t *mfs_hash_pool_usable_blkmap_sem;
sem_t *mfs_hash_pool_LRU_sem;
pthread_mutex_t	mfs_hash_lock;
/*init pools*/

void init_mfs_attr_pool();
void init_mfs_blkmap_pool();
void init_mfs_block_pool();
void init_mfs_hash_element_pool();
void init_mfs_hash_pool_container();

/*iterators*/

struct mfs_hash_attr* 	get_mfs_hash_attr(int e);
struct mfs_hash_blkmap* get_mfs_hash_blkmap(int e);
char* get_mfs_hash_blkmap_2(struct mfs_hash_element *e);

struct mfs_hash_block*	get_mfs_hash_block();
void add_mfs_LRU_list(struct mfs_hash_block *blk, struct mfs_hash_element *e, int index);
void clear_mfs_LRU_list(int i);

int put_mfs_hash_attr(struct mfs_hash_attr *data);
int put_mfs_hash_blkmap(struct mfs_hash_blkmap *data);
int put_mfs_hash_attr_2();
int put_mfs_hash_blkmap_2();

/*hash functions*/

void add_mfs_hash(struct mfs_hash_element *e);
struct mfs_hash_element* find_mfs_hash(char *path);
void del_mfs_hash(struct mfs_hash_element *e);

/* server hash functions */

void add_mfs_serv_hash(struct mfs_hash_serv_element *e);
struct mfs_hash_serv_element* find_mfs_serv_hash(char *path);
void del_mfs_serv_hash(struct mfs_hash_serv_element *e);

#endif
