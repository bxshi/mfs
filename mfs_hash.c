//#define _MFS_HASH_TEST_


#include "mfs_hash.h"
#include <time.h>

void init_mfs_hash_pool_container()
{
	int shm=0;
	size_t size;
	
	shm = shm_open("/mfs_hash_pool_container", O_CREAT|O_RDWR, 0666);
	
	size = sizeof(struct mfs_hash_pool_container);
	
	ftruncate(shm, size);
	
	mfs_hash_pool_stat_p = (struct mfs_hash_pool_container *)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm, 0);
	
	/*semaphore, add it or not?*/	
	if(mfs_hash_pool_attr_sem == NULL)
		mfs_hash_pool_attr_sem = sem_open("/mfs_hash_pool_attr_sem", O_CREAT|O_RDWR, 0666, 1);
	if(mfs_hash_pool_blkmap_sem == NULL)
		mfs_hash_pool_blkmap_sem = sem_open("/mfs_hash_pool_blkmap_sem", O_CREAT|O_RDWR, 0666, 1);
	if(mfs_hash_pool_block_sem == NULL)
		mfs_hash_pool_block_sem = sem_open("/mfs_hash_pool_block_sem", O_CREAT|O_RDWR, 0666, 1);
	if(mfs_hash_pool_usable_attr_sem == NULL)
		mfs_hash_pool_usable_attr_sem = sem_open("/mfs_hash_pool_usable_attr_sem", O_CREAT|O_RDWR, 0666, 1);
	if(mfs_hash_pool_usable_blkmap_sem == NULL)
		mfs_hash_pool_usable_blkmap_sem = sem_open("/mfs_hash_pool_usable_blkmap_sem", O_CREAT|O_RDWR, 0666, 1);
	if(mfs_hash_pool_LRU_sem == NULL)
		mfs_hash_pool_LRU_sem = sem_open("/mfs_hash_pool_LRU_sem", O_CREAT|O_RDWR, 0666, 1);
	
}

void init_mfs_attr_pool()
{
	int i;
	time_t t;
	size_t size;
	int shm=0;
	
	size = sizeof(struct mfs_hash_attr) * MFS_HASH_POOL_MAX;
	
	t = time(0);

	//test shared memory
	shm = shm_open("/mfs_hash_attr",O_CREAT|O_RDWR, 0666);
	
	ftruncate(shm, size);
	
	i=0;
	
	
	i = (mfs_hash_pool_stat_p->p_attr)++;
	(mfs_hash_pool_stat_p->attr[i]) = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm, 0);
	
}

void init_mfs_blkmap_pool()
{
	int i;
	char path[1024]="";
	time_t t;
	size_t size;
	int shm=0;
	
	size = sizeof(struct mfs_hash_blkmap) * MFS_HASH_POOL_MAX;
	
	t = time(0);
	sprintf(path, "/mfs_hash_blkmap%s", ctime(&t));
	shm = shm_open(path, O_CREAT|O_RDWR, 0666);
	
	ftruncate(shm, size);
	
	i=0;
	
	
	i = (mfs_hash_pool_stat_p->p_blkmap)++;
	
	mfs_hash_pool_stat_p->blkmap[i] = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm, 0);
	
}

void init_mfs_block_pool()
{
	int i;
	char path[1024]="";
	void *smp;
	time_t t;
	size_t size;
	int shm=0;
	
	size = sizeof(struct mfs_hash_block);
	
	t = time(0);
	sprintf(path, "/mfs_hash_block%s", ctime(&t));
	shm = shm_open(path, O_CREAT|O_RDWR, 0666);
	
	ftruncate(shm, size * MFS_HASH_POOL_MAX);
	smp =  mmap(NULL, size * MFS_HASH_POOL_MAX, PROT_READ|PROT_WRITE, MAP_SHARED, shm, 0);
	
	for(i=0;i<MFS_HASH_POOL_MAX-1;i++) {

		((struct mfs_hash_block *)(smp+i*size))->next = (struct mfs_hash_block *)(smp+(i+1)*size);
		((struct mfs_hash_block *)(smp+i*size))->prev = NULL;

	}
	
	
	((struct mfs_hash_block *)(smp+i*size))->next = mfs_hash_pool_stat_p->block;
	((struct mfs_hash_block *)(smp+i*size))->prev = NULL;
	
	mfs_hash_pool_stat_p->block = (struct mfs_hash_block *)smp;
	mfs_hash_pool_stat_p->block_cnt++;
	
}

void init_mfs_hash_element_pool()
{
	init_mfs_hash_pool_container();
	memset(mfs_hash_pool_stat_p, 0, sizeof(struct mfs_hash_pool_container));
	
	sem_wait(mfs_hash_pool_attr_sem);
	init_mfs_attr_pool();
	sem_post(mfs_hash_pool_attr_sem);

	sem_wait(mfs_hash_pool_blkmap_sem);
	init_mfs_blkmap_pool();
	sem_post(mfs_hash_pool_blkmap_sem);

	sem_wait(mfs_hash_pool_block_sem);
	init_mfs_block_pool();
	sem_post(mfs_hash_pool_block_sem);
}

struct mfs_hash_attr* 	get_mfs_hash_attr(int e)
{
	int index, offset;
	
	if(e > mfs_hash_pool_stat_p->usable_attr)
		return NULL;
	
	index = e / MFS_HASH_POOL_MAX;
	offset = e % MFS_HASH_POOL_MAX;

	return (struct mfs_hash_attr *)((mfs_hash_pool_stat_p->attr[index]) + sizeof(struct mfs_hash_attr)*offset);
}


char* get_mfs_hash_blkmap_2(struct mfs_hash_element *e)
{
	char path[255];
	int shm;
	time_t t;
	struct mfs_hash_attr *a;
	char *addr;

	t = time(0);
	sprintf(path, "/mfs_hash_blkmap%s", ctime(&t));
	a = &(e->attr);
	ftruncate(shm, a->st_size/ 4096 + 4096);
	addr = mmap(NULL, a->st_size / 4096 + 4096, PROT_READ|PROT_WRITE, MAP_SHARED, shm, 0);
	return addr;
}



struct mfs_hash_blkmap* get_mfs_hash_blkmap(int e)
{
	int index, offset;
	
	if(e > mfs_hash_pool_stat_p->usable_blkmap)
		return NULL;
	
	index = e / MFS_HASH_POOL_MAX;
	offset = e % MFS_HASH_POOL_MAX;
	
	return (struct mfs_hash_blkmap *)(mfs_hash_pool_stat_p->blkmap[index] + sizeof(struct mfs_hash_blkmap)*offset);
}

struct mfs_hash_block*	get_mfs_hash_block()
{
	struct mfs_hash_block *e;
	
	sem_wait(mfs_hash_pool_block_sem);
	e = mfs_hash_pool_stat_p->block;
	mfs_hash_pool_stat_p->block = e->next;
	if (e->next == NULL){
		sem_wait(mfs_hash_pool_LRU_sem);
		if(mfs_hash_pool_stat_p->block_cnt >= 512)
			clear_mfs_LRU_list(MFS_HASH_POOL_MAX * 2);
		else {
			init_mfs_block_pool();
		}
		sem_post(mfs_hash_pool_LRU_sem);
	}
	sem_post(mfs_hash_pool_block_sem);
	e->prev = NULL;
	e->next = NULL;
	return e;
}

void clear_mfs_LRU_list(int i)
{
	struct mfs_hash_block *blk;
	int j=0;
	blk = mfs_hash_pool_stat_p->LRU_list;
	if(mfs_hash_pool_stat_p->LRU_list_end == NULL) {
		while(blk->next!=NULL){
			blk = blk->next;
			j++;
		}
		mfs_hash_pool_stat_p->LRU_list_end = blk;
	}	
	
	while(i>5) {
		blk = mfs_hash_pool_stat_p->LRU_list_end;
		mfs_hash_pool_stat_p->LRU_list_end = blk->prev;
		blk->e->blkmap_addr[blk->index] = 0;
		blk->e->blkmap_mem[blk->index] = 0x0;
		blk->prev = NULL;
		blk->next = mfs_hash_pool_stat_p->block;
		mfs_hash_pool_stat_p->block = blk;
		i--;
	}

}
void add_mfs_LRU_list(struct mfs_hash_block *blk, struct mfs_hash_element *e, int index)
{
	if(blk->prev != NULL)
		blk->prev->next = blk->next;
	blk->prev = NULL;
	blk->next = mfs_hash_pool_stat_p->LRU_list;
	if(mfs_hash_pool_stat_p->LRU_list != NULL)
		mfs_hash_pool_stat_p->LRU_list->prev = blk;
	mfs_hash_pool_stat_p->LRU_list = blk;
	blk->index = index;
	blk->e = e;
}
int put_mfs_hash_attr_2()
{
	int i;

	sem_wait(mfs_hash_pool_usable_attr_sem);
	i = mfs_hash_pool_stat_p->usable_attr++;
	if(i+1 % MFS_HASH_POOL_MAX == 0)
		init_mfs_attr_pool();
	sem_post(mfs_hash_pool_usable_attr_sem);
	
	return i;
}
int put_mfs_hash_attr(struct mfs_hash_attr *data)
{
	int i;

	if (data == NULL)
		return -1;
		
	sem_wait(mfs_hash_pool_usable_attr_sem);
	i = mfs_hash_pool_stat_p->usable_attr++;
	if(i+1 % MFS_HASH_POOL_MAX == 0)
		init_mfs_attr_pool();
	sem_post(mfs_hash_pool_usable_attr_sem);
		
	memcpy(get_mfs_hash_attr(i), data, sizeof(struct mfs_hash_attr));
	
	return i;
}
int put_mfs_hash_blkmap_2()
{
	int i;

	sem_wait(mfs_hash_pool_usable_blkmap_sem);
	i = mfs_hash_pool_stat_p->usable_blkmap++;
	if(i+1 % MFS_HASH_POOL_MAX == 0)
		init_mfs_blkmap_pool();
	sem_post(mfs_hash_pool_usable_blkmap_sem);
	
	return i;
}
int put_mfs_hash_blkmap(struct mfs_hash_blkmap *data)
{
	int i;
	
	if (data == NULL)
		return -1;

	sem_wait(mfs_hash_pool_usable_blkmap_sem);
	i = mfs_hash_pool_stat_p->usable_blkmap++;
	if(i+1 % MFS_HASH_POOL_MAX == 0)
		init_mfs_blkmap_pool();
	sem_post(mfs_hash_pool_usable_blkmap_sem);
	
 	memcpy(get_mfs_hash_blkmap(i), data, sizeof(struct mfs_hash_blkmap));
	
	return i;
	
}

void add_mfs_hash(struct mfs_hash_element *e)
{
	HASH_ADD_STR(mfs_hash_table, path, e);
}
void add_mfs_serv_hash(struct mfs_hash_serv_element *e)
{
	HASH_ADD_STR(mfs_hash_serv_table, path, e);
}
struct mfs_hash_element* find_mfs_hash(char *path)
{
	struct mfs_hash_element *e;
	HASH_FIND_STR(mfs_hash_table, path, e);
	return e;
}
struct mfs_hash_serv_element* find_mfs_serv_hash(char *path)
{
	struct mfs_hash_serv_element *e;
	HASH_FIND_STR(mfs_hash_serv_table, path, e);
	return e;
}
void del_mfs_hash(struct mfs_hash_element *e)
{
	HASH_DEL(mfs_hash_table, e);
	free(e);
}
void del_mfs_serv_hash(struct mfs_hash_serv_element *e)
{
	HASH_DEL(mfs_hash_serv_table, e);
	free(e->data);
	free(e);
}
/* Test Main func */
#ifdef _MFS_HASH_TEST_

int main(int argc, char **argv)
{
	init_mfs_hash_element_pool();


	printf("Step 1 === Create mfs_hash_element\n");

	struct mfs_hash_element *e;
	char path1[1024];
	struct stat st;
	struct mfs_hash_blkmap blkmap;

	strcpy(path1,"autorun.inf\n");
	e = malloc(sizeof(struct mfs_hash_element)+strlen(path1)+1);
	strcpy(e->path, path1);
	e->attr = put_mfs_hash_attr(&st);
	e->blkmap = put_mfs_hash_blkmap(&blkmap);

	printf("-element 1: %u\n",e);
	printf("--attr %d\n",e->attr);
	printf("--attr addr %u\n",get_mfs_hash_attr(e->attr));
	printf("--blkmap %d\n", e->blkmap);
	printf("--blkmap addr %u\n",get_mfs_hash_blkmap(e->blkmap));
	
	add_mfs_hash(e);

	e = NULL;

	strcpy(path1,"test\n");
	e = malloc(sizeof(struct mfs_hash_element)+strlen(path1)+1);
	strcpy(e->path, path1);
	e->attr = put_mfs_hash_attr(NULL);
	e->blkmap = put_mfs_hash_blkmap(NULL);


	printf("-element 1: %u\n",e);
	printf("--attr %d\n",e->attr);
	printf("--attr addr %u\n",get_mfs_hash_attr(e->attr));
	printf("--blkmap %d\n", e->blkmap);
	printf("--blkmap addr %u\n",get_mfs_hash_blkmap(e->blkmap));

	add_mfs_hash(e);

	e = NULL;
	
	printf("Step 2 === get mfs_hash_element\n");

	e = find_mfs_hash("autorun.inf\n");
	printf("-autorun.inf\n");
	printf("-element 1: %u\n",e);
	printf("--attr %d\n",e->attr);
	printf("--attr addr %u\n",get_mfs_hash_attr(e->attr));
	printf("--blkmap %d\n", e->blkmap);
	printf("--blkmap addr %u\n",get_mfs_hash_blkmap(e->blkmap));

	e = find_mfs_hash("test\n");
	printf("-test\n");
	printf("-element 2: %u\n",e);
	printf("--attr %d\n",e->attr);
	printf("--attr addr %u\n",get_mfs_hash_attr(e->attr));
	printf("--blkmap %d\n", e->blkmap);
	printf("--blkmap addr %u\n",get_mfs_hash_blkmap(e->blkmap));

	printf("Step 3 === delete hash_element\n");
	printf("-delete test\n");
	del_mfs_hash(find_mfs_hash("test\n"));
	e = find_mfs_hash("test\n");
	printf("--addr %u\n",e);

	printf("Step 4 === change hash_element\n");
	e = find_mfs_hash("autorun.inf\n");
	e->blkmap=999;
	
	printf("autorun addr %u\n", e);
	e = find_mfs_hash("autorun.inf\n");
	printf("blkmap %d\n", e->blkmap);

return 0;
}

#endif
