/*
 * =====================================================================================
 *
 *       Filename:  mfs_client.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  12/30/2011 10:09:03 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Baoxu Shi (), bxshi.nku@gmail.com
 *        Company:  Nankai University, Tianjin, China
 *
 * =====================================================================================
 */
#define FUSE_USE_VERSION 26
//#define MFS_READ_DEBUG
//#define MFS_DEBUG
//if do not want these features, just commet it
//#define MFS_DEBUG_CACHE
//#define MFS_DEBUG_LRU
#define MFS_IZONE //if test for iozne -a define this
#define MFS_HAVE_READHEAD
#define MFS_HAVE_READHEAD_SEQ
#define MFS_HAVE_CACHE
#define MFS_HAVE_DISK_CACHE

#define SLEEP_TIME 1000 //when list is empty
#define WAIT_TIME 3000 //when other operation is working
#define MFS_LRU_LIST_MAX 2048
#define MFS_LRU_HD_LIST_MAX 1024

#include <pthread.h>
#include <signal.h>
#include <fuse.h>
#include <fuse/fuse_opt.h>
#include <sys/time.h>
#include <string.h>
#include <utime.h>

#include "mfs_file.h"
#include "mfs_net.h"
#include "mfs_hash.h"
#include "mfs_opt.h"
#include "sysusage.h"
#include "utlist.h"

int LRU_CURRENT_SIZE=0;
int HD_CURRENT_SIZE=0;

#ifdef MFS_DEBUG_CACHE
unsigned long mfs_cache_hit=0;
unsigned long mfs_hd_cache_hit=0;
unsigned long mfs_mem_cache_hit=0;
unsigned long mfs_cache_miss=0;
float mfs_cache_precentage=0;
#endif

/*
fuse
*/

struct fuse *fuse;
struct fuse_chan *ch;
char *mountpoint;

struct IO_watcher{
		float cpu;
		float disk;
} watcher;

struct mfsLRU {
	struct UT_hash_handle hh;
	char *addr;
	struct mfs_hash_element *e;
	off_t index;
	int size;
	int len;
	struct mfsLRU *next;
	struct mfsLRU *prev;
};

struct mfshdLRU {
	struct UT_hash_handle hh;
	struct mfsLRU *lru_element;
	char path[];
};
struct mfshdLRU *mfs_hd_name_list=NULL;

struct mfsUpload {
	struct UT_hash_handle hh;
	off_t offset;
	size_t size;
	struct mfs_hash_element *e;
	struct mfsUpload *next;
};
struct mfsUpload *mfs_upload_list=NULL;
pthread_mutex_t mfs_upload_lock;

struct mfsLRU *mfs_LRU_list=NULL;
size_t mfs_LRU_list_size=0;
struct mfsLRU *mfs_LRU_hd_list=NULL;
size_t mfs_LRU_hd_list_size=0;

int mfs_LRU_list_count=0;
struct mfsLRU *mfs_LRU_task=NULL;
pthread_mutex_t mfs_LRU_lock;
pthread_mutex_t mfs_LRU_hd_lock;
char *mfs_LRU_hd_blank=NULL;

int mfs_stop_thread=0;
pthread_t cache_tid, cache_tid1, cache_tid2,cache_tid3, cache_tid4, cache_clean_tid, hd_cache_clean_tid, ra_lv1_tid, ra_tid1, ra_tid2, ra_tid3, ra_tid4, ra_tid5, ra_tid6, ra_seq_tid, upload_tid, network_tid, io_watcher_tid;

static void safe_quit_mfs(int tmp);
static void mfs_network_disconnect(int tmp);

void mfs_net_restart()
{
	pthread_mutex_lock(&mfs_task_lock);
	mfs_net_connect = 0;
	pthread_mutex_unlock(&mfs_task_lock);
}

void add_mfs_upload_task(off_t offset, size_t size, struct mfs_hash_element *e)
{
		struct mfsUpload *upload=NULL;
		upload = malloc(sizeof(struct mfsUpload));
		upload->offset = offset;
		upload->size = size;
		upload->e = e;
		upload->next = NULL;

		pthread_mutex_lock(&mfs_upload_lock);
		upload->next= mfs_upload_list;
		mfs_upload_list = upload;
		pthread_mutex_unlock(&mfs_upload_lock);
}

int mfs_do_upload_task(struct mfsUpload *upload)
{
	struct mfs_task *task;
	int net_res, res;
	char *cache_path;
	int fd;
	char *buf;
	task = get_mfs_task(task);
		if(task == NULL)
			return -ENOENT;

	net_res = send_mfs_command(task->connfd, MFS_WRITE, mfs_strlen(upload->e->path));	

	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}

	net_res = send_mfs_path_name(task->connfd, upload->e->path);

	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}

	net_res = send_mfs_read_info(task->connfd, upload->offset, upload->size);

	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}

/* read from disk */
	cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(upload->e->path) + 5);
	sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, upload->e->path, upload->offset / (MFS_DISK_CACHE_SPLIT_SIZE * MFS_BLOCK_SIZE));
	fd = open(cache_path, O_RDONLY);

	buf = malloc(upload->size);

//	res = pread(fd, buf, upload->size, ((upload->offset / MFS_BLOCK_SIZE) % MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
	res = pread(fd, buf, upload->size, upload->offset - ((upload->offset / MFS_BLOCK_SIZE) / MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
	close(fd);

	if(res != upload->size){
			put_mfs_task(task);
#ifdef MFS_DEBUG
			printf("upload error, can not get data from local disk\n");
#endif
			return -1;
	}
	
	net_res = send_mfs_read_buff(task->connfd, buf, res);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}

	net_res = get_mfs_reply(task->connfd, &res);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}
	put_mfs_task(task);

	return res;
}

void *mfs_IO_watcher(void *argc)
{
		struct cpu_stat pre_cpu, after_cpu;
		struct disk_stat pre_disk, after_disk;

		pre_cpu = get_cpu_stat();
		pre_disk = get_disk_stat();

		while(1){
				usleep(MFS_IO_WATCHER_EVAL);
				after_cpu = get_cpu_stat();
				after_disk = get_disk_stat();

				/* update */
				watcher.cpu = get_cpu_usage(pre_cpu, after_cpu);
				watcher.disk = get_disk_usage(pre_disk, after_disk, MFS_IO_WATCHER_EVAL);
				
				pre_cpu = after_cpu;
				pre_disk = after_disk;
				if(mfs_stop_thread == 1){
					printf("%s terminated\n", __func__);
					pthread_exit(0);
				}
		}
}

void *mfs_upload_task(void *argc)
{
	struct mfsUpload *upload = NULL;
	int res;
	int i;

	pthread_mutex_init(&mfs_upload_lock, NULL);
	mfs_upload_list = NULL;
	while(1){
			while(watcher.disk>MFS_UPLOAD_ACTIVE_IO_BARRIER || watcher.cpu >MFS_UPLOAD_ACTIVE_CPU_BARRIER){
				for(i=0;i<5;i++){
					if(mfs_stop_thread == 1){
						printf("%s terminated\n", __func__);
						pthread_exit(0);
					}
					sleep(1);
				}
			}

			pthread_mutex_lock(&mfs_upload_lock);
			if(mfs_upload_list==NULL){
					pthread_mutex_unlock(&mfs_upload_lock);
					usleep(SLEEP_TIME);
			}else{
					upload = mfs_upload_list;
					mfs_upload_list = upload->next;
					pthread_mutex_unlock(&mfs_upload_lock);
					/* upload */
					res = mfs_do_upload_task(upload);
					if(res == -1){/* upload error */
						pthread_mutex_lock(&mfs_upload_lock);
						upload->next = mfs_upload_list;
						mfs_upload_list = upload;
						pthread_mutex_unlock(&mfs_upload_lock);
					}else{
						/* add to LRU */
						free(upload);
					}
			}
			if(mfs_stop_thread == 1){
				printf("%s terminated\n", __func__);
				pthread_exit(0);
			}
	}

}


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  clean_mfs_LRU_hd_element
 *  Description:  check status of LRU-hd elements, if full, clean it to MFS_HD_CLEAN_SIZE
 * =====================================================================================
 */
void *clean_mfs_LRU_hd_element (void *args)
{
		struct mfsLRU *tmp_del=NULL;
		struct mfshdLRU *hd_name=NULL, *tmp_hd_name=NULL;
		int delete = 0;
		int i;
		while(1){

				if(mfs_stop_thread == 1){/* stop this thread */
					printf("%s terminated\n", __func__);
					pthread_exit(0);
				}

				/* deal with cache full */
				if(HD_CURRENT_SIZE >= MFS_DISK_CACHE_SIZE){
					pthread_mutex_lock(&mfs_LRU_hd_lock);
					while(HD_CURRENT_SIZE >= MFS_HD_CLEAN_SIZE){
#ifdef MFS_DEBUG
			printf("mfs_LRU_hd_list_size %d > MFS_DISK_CACHE_SIZE %d\n", HD_CURRENT_SIZE, MFS_DISK_CACHE_SIZE);
#endif
						HASH_ITER(hh, mfs_hd_name_list, hd_name, tmp_hd_name){
#ifdef MFS_DEBUG
			printf("hd LRU full, delete %p\n", hd_name);
			printf("delete %p\n", hd_name);
#endif
						while(hd_name->lru_element!=NULL){/* update blkmap */
								tmp_del = hd_name->lru_element;
								hd_name->lru_element = hd_name->lru_element->next;

								HD_CURRENT_SIZE -=tmp_del->len;
								if(tmp_del->e->blkmap_addr[tmp_del->index] == 2){
								memset(&tmp_del->e->blkmap_addr[tmp_del->index], 0, sizeof(int) * (tmp_del->size));
								memset(&tmp_del->e->blkmap_continue[tmp_del->index], 0, sizeof(char) * (tmp_del->size));
								memset(&tmp_del->e->blkmap_LRU[tmp_del->index], 0, sizeof(char *) * (tmp_del->size));
								}else {
									delete =1;
								}
#ifdef MFS_DEBUG_LRU
		printf("delete hd LRU offset %zu size %zu\n", tmp_del->index, tmp_del->len);
#endif
								free(tmp_del);
						}

						HASH_DELETE(hh, mfs_hd_name_list, hd_name);
						if(delete == 0)
							remove(hd_name->path);//delete this big cache block

						free(hd_name);

						break;
					}
				}
					pthread_mutex_unlock(&mfs_LRU_hd_lock);
			}
				for(i=0;i<MFS_HD_CLEAN_TIME;i++){
					sleep(1);
					if(mfs_stop_thread == 1){/* stop this thread */
						printf("%s terminated\n", __func__);
						pthread_exit(0);
					}
				}
		}
}		/* -----  end of function clean_mfs_LRU_hd_element  ----- */

void add_mfs_LRU_hd_element(struct mfsLRU *lru_element)
{
	char *cache_path;
	struct mfshdLRU *hd_name;
	pthread_mutex_lock(&mfs_LRU_hd_lock);
	cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(lru_element->e->path) + 5);
	sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, lru_element->e->path, lru_element->index / MFS_DISK_CACHE_SPLIT_SIZE);

	HASH_FIND_STR(mfs_hd_name_list, cache_path, hd_name);

	if(hd_name == NULL){/* no such file exists */
		hd_name = malloc(sizeof(struct mfshdLRU) + MFS_CACHE_PATH_LEN + mfs_strlen(lru_element->e->path) + 5);
		hd_name->lru_element = NULL;
		strcpy(hd_name->path, cache_path);
#ifdef MFS_DEBUG
		printf("hd_name->path is %s\n", hd_name->path);
#endif
		lru_element->next = hd_name->lru_element;
		hd_name->lru_element = lru_element;
#ifdef MFS_DEBUG_LRU
		printf("add hd LRU offset %zu size %zu", lru_element->index, lru_element->len);
#endif
		HASH_ADD_STR(mfs_hd_name_list, path, hd_name);
#ifdef MFS_DEBUG
		printf("add %p\n", hd_name);
#endif
		HD_CURRENT_SIZE += lru_element->len;
		free(cache_path);
	}else{/* exists that file */
		free(cache_path);

		lru_element->next = hd_name->lru_element;
		hd_name->lru_element = lru_element;
		lru_element->addr = hd_name;
		HD_CURRENT_SIZE +=lru_element->len;

		HASH_DELETE(hh, mfs_hd_name_list, hd_name);
		HASH_ADD_STR(mfs_hd_name_list, path, hd_name);
	}
			
	pthread_mutex_unlock(&mfs_LRU_hd_lock);

}
void update_mfs_LRU_hd_element(char *cache_path)
{
	struct mfshdLRU *hd_name;
	HASH_FIND_STR(mfs_hd_name_list, cache_path, hd_name);
	if(hd_name == NULL)
		return;
#ifdef MFS_DEBUG
	printf("update %p\n", hd_name);
#endif
	HASH_DELETE(hh, mfs_hd_name_list, hd_name);
	HASH_ADD_STR(mfs_hd_name_list, path, hd_name);
}

struct mfsLRU* add_mfs_LRU_element(char *addr, struct mfs_hash_element *e, int index, int size, int len, int type)
{
	struct mfsLRU *lru_element=NULL, *tmp1=NULL, *tmp2=NULL;
	int i;
	int fd;
	int res;
	char *cache_path;
	int lru_limit = 0.7 * MFS_LRU_LIST_MAX;
	lru_element = malloc(sizeof(struct mfsLRU));
	lru_element->addr = addr;
	lru_element->e = e;
	lru_element->index = index;
	lru_element->size = size;
	lru_element->len = len;

	pthread_mutex_lock(&mfs_LRU_lock);
	/* add to hash */
#ifdef MFS_DEBUG
	printf("add %u\n", lru_element);
#endif
	switch(type) {
		case 1:
		case 0:
			DL_PREPEND(mfs_LRU_list, lru_element);
			break;
		case 2:
			DL_APPEND(mfs_LRU_list, lru_element);
			break;
		default:
			DL_PREPEND(mfs_LRU_list, lru_element);
			break;
	}
	LRU_CURRENT_SIZE++;
#ifdef MFS_DEBUG_LRU
	printf("add LRU offset %zu size %zu\n", lru_element->index ,lru_element->len);
#endif
	if(LRU_CURRENT_SIZE >= MFS_LRU_LIST_MAX) {
	while(LRU_CURRENT_SIZE >= lru_limit) {
		tmp1 = mfs_LRU_list->prev;
		if(tmp1 == lru_element)
			tmp1 = tmp1->prev;
		DL_DELETE(mfs_LRU_list, tmp1);
#ifdef MFS_DEBUG_LRU
	printf("delet LRU offset %zu size %zu\n", tmp1->index ,tmp1->len);
#endif
		LRU_CURRENT_SIZE--;
#ifdef MFS_DEBUG
			printf("LRU full, delete %u\n", tmp1);
#endif
			if(tmp1->e->blkmap_addr[tmp1->index] != 1){
				pthread_mutex_unlock(&mfs_LRU_lock);
				return lru_element;
			}

#ifdef MFS_HAVE_DISK_CACHE			
	#ifdef MFS_DEBUG
			printf("write to hdisk\n");
			printf("%s cache_path size=%d\n", __func__, MFS_CACHE_PATH_LEN + mfs_strlen(tmp1->e->path)+5);
	#endif
			cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(tmp1->e->path) + 5);
	#ifdef MFS_DEBUG
			printf("%s cache_path is %s%s-%lu\n",__func__,  MFS_CACHE_PATH, tmp1->e->path, tmp1->index / MFS_DISK_CACHE_SPLIT_SIZE);
	#endif
			sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, tmp1->e->path, tmp1->index / MFS_DISK_CACHE_SPLIT_SIZE);
			fd = open(cache_path, O_CREAT|O_RDWR, 0777);
	#ifdef MFS_DEBUG
			if(fd == -1)
					printf("hdisk cache open error %s %s \n", (char *)strerror(errno), cache_path);
			else
					printf("write to file %s\n", cache_path);
	#endif
			res = pwrite(fd , tmp1->addr, tmp1->len, (tmp1->index % MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
	#ifdef MFS_DEBUG
			if(res == -1)
					printf("write error %s %s \n", (char *)strerror(errno), cache_path);
	#endif
			close(fd);
			free(cache_path);
	#ifdef MFS_DEBUG
			if(res == tmp1->len)
				printf("write ok\n");
			else
				printf("write error, should write %d, actually write %d\n", tmp1->len, res);
	#endif
			for(i=0;i<tmp1->size;i++)
				tmp1->e->blkmap_addr[tmp1->index + i] = 2;

			free(tmp1->addr);
			tmp1->addr = NULL;
			add_mfs_LRU_hd_element(tmp1);
			memset(&tmp1->e->blkmap_mem[tmp1->index], 0, sizeof(char *) * (tmp1->size));

#else
			/* no disk cache, which means if LRU is full, then this would be deleted */
			memset(&tmp1->e->blkmap_addr[tmp1->index], 0, sizeof(int) * (tmp1->size));
			memset(&tmp1->e->blkmap_continue[tmp1->index], 0, sizeof(char) * (tmp1->size));
			memset(&tmp1->e->blkmap_LRU[tmp1->index], 0, sizeof(char *) * (tmp1->size));
			free(tmp1->addr);
			free(tmp1);
#endif
		}
	}

	pthread_mutex_unlock(&mfs_LRU_lock);
	return lru_element;
}


void update_mfs_LRU_element(struct mfsLRU *lru_element)
{
	DL_DELETE(mfs_LRU_list, lru_element);
	DL_PREPEND(mfs_LRU_list, lru_element);
#ifdef MFS_DEBUG
	printf("Update LRU %u\n", lru_element);
#endif
}
struct testcache {
	size_t offset;
	char *addr;
	UT_hash_handle hh;
};
struct testcache *test_cache_list=NULL;

struct memcache {
	size_t size;
	size_t offset;
	struct mfs_hash_element *e;
	char *buff;
	struct memcache *next;
	int type;
};

struct memcache *mem_list=NULL;
struct memcache *mem_list_ra=NULL;
pthread_mutex_t mem_list_lock;

struct readhead {
	size_t size;
	size_t offset;
	struct mfs_hash_element *e;
	struct readhead *next;
	int type;/* 0:normal IO 1:in time RA 2:normal RA */
};

/* Readahead 4-Level check */
/* level 1 */
struct readhead *ra_current_list=NULL;
pthread_mutex_t ra_current_lock;
/* level 2 */
struct readhead *ra_recent_list=NULL;
pthread_mutex_t ra_recent_lock;
/* level 3 */
struct readhead *ra_current_dir_list=NULL;
struct mfs_hash_element *ra_current_dir=NULL;
pthread_mutex_t ra_current_dir_lock;
/* level 4 */
struct readhead *ra_normal_list=NULL;
pthread_mutex_t ra_normal_lock;
/* Readhead 4-Level end */

/* Readhead Operations */

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  mfs_ra_insert_element
 *  Description:  WARNING, No semaphore locks
 * =====================================================================================
 */
void mfs_ra_insert_element (struct readhaed *dest, struct mfs_hash_element *e, size_t size, size_t offset)
{
	struct readhead *ra = malloc(sizeof(struct readhead));
	ra->e = e;
	ra->offset = offset;
	ra->size = size;
	
	ra->next = dest;
	dest = ra;
}		/* -----  end of function mfs_ra_insert_element  ----- */

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  mfs_ra_check_current_list
 *  Description:  update current ra file(Level 1), move previous task to Level 2
 *  		  Got BUGS here, if use ra as a link element, insert it to other links
 *  		  may ruin original lists.
 *  		  When do read, check this
 * =====================================================================================
 */
void mfs_ra_check_current_list(struct readhead *ra)
{
	pthread_mutex_lock(&ra_current_lock);
	if(ra_current_list != ra && ra->offset < ra->e->attr.st_size) {/* not current file */
		ra_current_list = ra; 
		printf("RA level 1 change to %s\n", ra->e->path);
	}
	pthread_mutex_unlock(&ra_current_lock);
}

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  mfs_ra_do_current_list
 *  Description:  
 * =====================================================================================
 */
void mfs_ra_do_current_list (void *argc)
{
	int res;
	while(1){
				if(mfs_stop_thread == 1){
					printf("%s terminated\n", __func__);
					pthread_exit(0);
				}
		pthread_mutex_lock(&ra_current_lock);
		if(ra_current_list != NULL) {
			res = mfs_readhead(ra_current_list);
			printf("RA level1 read %d\n", res);
			if(res == -1)/* already read or network error, need to add solution with network error */
				ra_current_list->offset += ra_current_list->size * 20;
			else
				ra_current_list->offset += ra_current_list->size;
			if(ra_current_list->offset >= ra_current_list->e->attr.st_size)/* finished ra */
				ra_current_list = NULL;
			pthread_mutex_unlock(&ra_current_lock);

		}else{
			pthread_mutex_unlock(&ra_current_lock);
			usleep(WAIT_TIME);
		}
		
	}
}		/* -----  end of function mfs_ra_do_current_list  ----- */

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  mfs_ra_check_current_dir
 *  Description:  update current directory
 * =====================================================================================
 */
void mfs_ra_check_current_dir (struct mfs_hash_element *e)
{
	pthread_mutex_lock(&ra_current_dir_lock);
	if(ra_current_dir != NULL)
		if(ra_current_dir != e)
			ra_current_dir = e;
	else {
		
	}
	pthread_mutex_unlock(&ra_current_dir_lock);

}		/* -----  end of function mfs_ra_check_current_dir  ----- */

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  mfs_ra_insert_list
 *  Description:  add src to dest's head
 * =====================================================================================
 */
void mfs_ra_insert_list(struct readhead *dest, pthread_mutex_t *dest_lock, struct readhead *src, pthread_mutex_t *src_lock)
{
	struct readhead *tmp_src, *tmp;
	pthread_mutex_lock(src_lock);
	if(src == NULL)
		return;
	tmp_src = src;
	src = NULL;
	pthread_mutex_unlock(src_lock);

	pthread_mutex_lock(dest_lock);
	while(tmp_src!=NULL){
		tmp = tmp_src;
		tmp_src = tmp_src->next;
		tmp->next = dest;
		dest = tmp;
	}
	pthread_mutex_unlock(dest_lock);
}

struct readhead *ra_list=NULL;
pthread_mutex_t ra_list_lock;
struct readhead *ra_list_seq_head=NULL;
struct readhead *ra_list_seq_end=NULL;
pthread_mutex_t ra_list_seq_lock;

void mfs_add_readhead_seq(size_t size, size_t offset, struct mfs_hash_element *e, int type)
{
	struct readhead *list;
	list = malloc(sizeof(struct readhead));
	list->size = size;
	list->offset = offset;
	list->e = e;
	list->next = NULL;
	list->type = type;
	
	pthread_mutex_lock(&ra_list_seq_lock);
	if(ra_list_seq_end != NULL)
		ra_list_seq_end->next = list;
	ra_list_seq_end = list;
	if(ra_list_seq_head == NULL)
		ra_list_seq_head = ra_list_seq_end;
	pthread_mutex_unlock(&ra_list_seq_lock);
}
void mfs_add_readhead(size_t size, size_t offset, struct mfs_hash_element *e, int type)
{
	struct readhead *list;
	list = malloc(sizeof(struct readhead));
	list->size = size;
	list->offset = offset;
	list->e = e;
	list->next = NULL;
	list->type = type;
	pthread_mutex_lock(&ra_list_lock);
	list->next = ra_list;
	ra_list = list;
	pthread_mutex_unlock(&ra_list_lock);
}
void *mfs_do_readhead_seq(void *argc)
{
	struct readhead *list;
	int res;
#ifndef MFS_HAVE_READHEAD_SEQ
	while(1)
		sleep(100000);
#endif
	pthread_mutex_init(&ra_list_seq_lock, NULL);
	ra_list_seq_head = NULL;
	ra_list_seq_end = NULL;
	while(1){

		while(watcher.cpu > MFS_RA_ACTIVE_CPU_BARRIER || watcher.disk > MFS_RA_ACTIVE_IO_BARRIER){
			usleep(WAIT_TIME);
				if(mfs_stop_thread == 1){
					printf("%s terminated\n", __func__);
					pthread_exit(0);
				}
		}
		pthread_mutex_lock(&ra_list_seq_lock);
		if(ra_list_seq_head !=NULL) {
			if(ra_list_seq_head == ra_list_seq_end)
				ra_list_seq_end = NULL;
			list = ra_list_seq_head;
			ra_list_seq_head = list->next;
			pthread_mutex_unlock(&ra_list_seq_lock);
#ifdef MFS_DEBUG
			printf("Do readhead %d at %d file %s\n", list->size, list->offset, list->e->path);
#endif
			res = mfs_readhead(list, 0);
			if(res != -1 && list->size + list->offset + list->size <= list->e->attr.st_size){
				mfs_add_readhead_seq(131072, list->offset+list->size, list->e, 2);
			}
			if(res == -1){
					mfs_add_readhead_seq(list->size, list->offset, list->e, 2);
					usleep(SLEEP_TIME);
			}
			free(list);
		}else {
			pthread_mutex_unlock(&ra_list_seq_lock);
			usleep(SLEEP_TIME);
		}

		if(mfs_stop_thread == 1){
			printf("%s terminated\n", __func__);
			pthread_exit(0);
		}
	}
}
void *mfs_do_readhead(void *argc)
{
	struct readhead *list;
	int res;
#ifndef MFS_HAVE_READHEAD
	while(1)
		sleep(100000);
#endif
	
	pthread_mutex_init(&ra_list_lock, NULL);
	ra_list = NULL;
	while(1){
		pthread_mutex_lock(&ra_list_lock);
		if(ra_list != NULL) {
			list = ra_list;
			ra_list = list->next;
			pthread_mutex_unlock(&ra_list_lock);
			res = mfs_readhead(list, 1);
			if(res == -1){
				mfs_add_readhead(list->size, list->offset, list->e, 1);
				usleep(SLEEP_TIME);
			}
			free(list);
		} else {
			pthread_mutex_unlock(&ra_list_lock);
			usleep(SLEEP_TIME);
		}
		if(mfs_stop_thread == 1){
			printf("%s terminated\n", __func__);
			pthread_exit(0);
		}
	}
}
void mfs_add_mem_cache(size_t size, size_t offset, struct mfs_hash_element *e, char *buff, int type, int LRU_type)
{
	struct memcache *list;
	list = malloc(sizeof(struct memcache));
	list->size = size;
	list->offset = offset;
	list->e = e;
	list->buff = buff;
	list->type = LRU_type;
	
	pthread_mutex_lock(&mem_list_lock);
	if(type == 0) {/* Normal IO*/
		list->next = mem_list;
		mem_list = list;
	}else{
		list->next = mem_list_ra;
		mem_list_ra = list;
	}
	pthread_mutex_unlock(&mem_list_lock);
}
void *mfs_save_mem_cache(void *argc)
{
#ifndef MFS_HAVE_CACHE
		return 0;
#endif
	int  j, save_offset, save_size;
	struct memcache *list;
	char *buff;
	struct mfs_hash_element *e;
	struct mfsLRU *element=NULL;
	mem_list = NULL;
	mem_list_ra = NULL;

	while(1){

MFS_SAVE_MEMCACHE:

		list = NULL;
		pthread_mutex_lock(&mem_list_lock);
		if(mem_list_ra != NULL) {
			list = mem_list_ra;
			mem_list_ra = list->next;
		}else if(mem_list != NULL) {
			if(mem_list !=NULL){
				list = mem_list;
				mem_list = list->next;
			}
		}
		if(list != NULL) {
			pthread_mutex_unlock(&mem_list_lock);
			save_offset = list->offset / MFS_BLOCK_SIZE;
			save_size = list->size / MFS_BLOCK_SIZE;
			e = list->e;
			buff = list->buff;
#ifdef MFS_DEBUG
			printf("save mem_cache %d at %zu %p, file%s\n", list->size, list->offset, list->buff, list->e->path);
#endif
			pthread_rwlock_wrlock(&(e->lock));
			element = list->size%MFS_BLOCK_SIZE == 0 ? add_mfs_LRU_element(buff, e, save_offset, save_size, list->size, list->type) : add_mfs_LRU_element(buff, e, save_offset, save_size+1, list->size, list->type);
			for(j=0;j<save_size;j++) {
				e->blkmap_mem[save_offset + j] = buff + j*MFS_BLOCK_SIZE;
				e->blkmap_size[save_offset + j] = MFS_BLOCK_SIZE;
				e->blkmap_addr[save_offset + j] = 1;
				e->blkmap_continue[save_offset+j] = 1;
				e->blkmap_LRU[save_offset+j] = element;
			}
			if(list->size % MFS_BLOCK_SIZE !=0) {
				e->blkmap_mem[save_offset + j] = buff + j*MFS_BLOCK_SIZE;
				e->blkmap_size[save_offset + j]= list->size % MFS_BLOCK_SIZE;
				e->blkmap_addr[save_offset + j] = 1;
				e->blkmap_continue[save_offset+j] = 0;
				e->blkmap_LRU[save_offset+j]=element;
			}else{
				e->blkmap_continue[save_offset+j-1] = 0;
			}
			pthread_rwlock_unlock(&(e->lock));
			free(list);

		}else{

			pthread_mutex_unlock(&mem_list_lock);
			usleep(SLEEP_TIME);

		}
		
		if(mfs_stop_thread == 1){
			printf("%s terminated\n", __func__);
			pthread_exit(0);
		}
	}
}

/* type 0 for normal RA read, type 1 for IO and instant RA */
int mfs_readhead(struct readhead *argc)
{
	struct readhead *ra;
	struct mfs_task *task;
	char *buff=NULL;
	size_t save_offset;
	int net_res, tmp_res;
	char *cache_path;
	int fd;
	
	ra = argc;
	save_offset = ra->offset / MFS_BLOCK_SIZE;
#ifndef MFS_IOZONE
	/* add RA from local disk */
	if(ra->e->attr.st_size == 0)
			return -2;
	if(ra->e->blkmap_addr[save_offset] == 2) {/* cache is on local disk */

				cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(ra->e->path) + 5);
				sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, ra->e->path, save_offset / MFS_DISK_CACHE_SPLIT_SIZE);
				fd = open(cache_path, O_RDONLY);

				buff = malloc(ra->size);
				
				net_res = pread(fd, buff, ra->size, (save_offset % MFS_DISK_CACHE_SPLIT_SIZE) * 4096);
				close(fd);

#ifdef MFS_HAVE_CACHE
				mfs_add_mem_cache(net_res, ra->offset, ra->e, buff, 1, ra->type);
#endif
				return net_res;
	}
#endif
	if(ra->e->blkmap_addr[save_offset] != 0 || ra->e->blkmap_size[save_offset]!=0)
		return -2;

	task = get_mfs_task(task);
		if(task == NULL)
			return -ENOENT;
	net_res = send_mfs_command(task->connfd, MFS_READ, mfs_strlen(ra->e->path));	
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}
	net_res = send_mfs_path_name(task->connfd, ra->e->path);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}
	net_res = send_mfs_read_info(task->connfd, ra->offset, ra->size);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}
	net_res = get_mfs_reply(task->connfd, &tmp_res);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -1;
	}
	if(tmp_res == 0){
		tmp_res = get_mfs_pack_info(task->connfd);
		if(tmp_res == -1)
			return -1;
		buff = malloc(tmp_res);
		get_mfs_read_buff(task->connfd, buff, tmp_res);
		put_mfs_task(task);

#ifdef MFS_HAVE_CACHE
		mfs_add_mem_cache(tmp_res, ra->offset, ra->e, buff, 1, ra->type);
#endif
		return tmp_res;
	}
	return -1;
}


void *mfs_network(void *argc){
		while(1){
				pthread_mutex_lock(&mfs_task_lock);
				if(mfs_net_connect == 0)
					init_mfs_task(MFS_CLIENT_SOCKET_THREADS, MFS_REMOTE_SERVER, MFS_REMOTE_PORT);
				pthread_mutex_unlock(&mfs_task_lock);
				sleep(10);
				if(mfs_stop_thread == 1){
					printf("%s terminated\n", __func__);
					pthread_exit(0);
				}
		}
}

static int mfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	int tmp;
	int net_res;
	int blk_size;
	char *cache_path;
	struct mfs_hash_element *e=NULL;
	struct mfs_hash_blkmap *blk=NULL;
	struct readhead *ra=NULL;
	pthread_t tid;
	struct mfs_task *task;
	/* hash check */

	HASH_FIND_STR(mfs_hash_table, path, e);


#ifdef MFS_DEBUG
	printf("check ok\n");
#endif 
	if(e!=NULL) {
#ifdef MFS_DEBUG
			printf("getattr hash got\n");
#endif 
		if(e->attr_err == 0) {
			memcpy(stbuf, &(e->attr), sizeof(struct stat));
			return 0;
		}
	
		return e->attr_err;
	}else if(e == NULL){
		e = malloc(sizeof(struct mfs_hash_element) + mfs_strlen(path));
#ifdef MFS_DEBUG
		printf("create new e %ud\n", e);
#endif
		e->attr_err = 0;
		e->dir_err = 0;
		strcpy(e->path, path);
	
	/* get data from remote */
		task = get_mfs_task();
		if(task == NULL)
				return -ENOENT;
		tmp = mfs_strlen(path);
		net_res = send_mfs_command(task->connfd, MFS_GETATTR, tmp);
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			free(e);
			return -ENOENT;
		}
		net_res = send_mfs_path_name(task->connfd, path);
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			free(e);
			return -ENOENT;
		}
		net_res = get_mfs_reply(task->connfd, &res);
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			free(e);
			return -ENOENT;
		}
	
		if(res == 0){ /*have following data package*/
			net_res = get_mfs_stat(task->connfd, stbuf);
			if (net_res == -1){
				put_mfs_task(task);
				mfs_net_restart();
				free(e);
				return -ENOENT;
			}
		}
		put_mfs_task(task);
		if(res != 0){/* error */
			e->attr_err = -res;
			e->blkmap_size = NULL;
			e->blkmap_addr = NULL;
			e->blkmap_mem = NULL;
			e->blkmap_LRU = NULL;
			e->blkmap_continue = NULL;
			e->blkmap_offset = NULL;
			e->dir_entry = NULL;
			e->symbol_link = NULL;
			pthread_rwlock_init(&(e->lock), NULL);
		}else{/* copy cache */
			memcpy(&(e->attr), stbuf, sizeof(struct stat));	
			if(S_ISREG(stbuf->st_mode)) {
				/* create blkmap */
				blk_size = stbuf->st_size / MFS_BLOCK_SIZE + 1;
				if(stbuf->st_size % MFS_BLOCK_SIZE!= 0)
					blk_size++;
		
#ifdef MFS_DEBUG
				printf("init new e %ud start\n", e);
#endif

				e->blkmap_offset = malloc(sizeof(int) * blk_size);
				memset(e->blkmap_offset, 0, sizeof(int) * blk_size);
				e->blkmap_size = malloc(sizeof(int) * blk_size);
				memset(e->blkmap_size, 0, sizeof(int) * blk_size);
				e->blkmap_addr = malloc(sizeof(int) * blk_size);
				memset(e->blkmap_addr, 0, sizeof(int) * blk_size);
				e->blkmap_mem = malloc(sizeof(char *) * blk_size);
				e->blkmap_continue= malloc(sizeof(char) * blk_size);
				memset(e->blkmap_continue, 0, sizeof(char) * blk_size);
				e->blkmap_LRU = malloc(sizeof(char *) * blk_size);
				memset(e->blkmap_LRU, 0, sizeof(char *) * blk_size);
				pthread_rwlock_init(&(e->lock), NULL);
				e->dir_entry = NULL;
				e->ra = malloc(sizeof(struct readhead));
				((struct readhead *)e->ra)->e = e;
				((struct readhead *)e->ra)->offset = 0;
				((struct readhead *)e->ra)->size = 131072;
				((struct readhead *)e->ra)->next = NULL;

#ifdef MFS_DEBUG
				printf("init new e %ud finish\n", e);
#endif
	#ifdef MFS_HAVE_READHEAD_SEQ
				/* readhead thread */
				mfs_add_readhead_seq(65536, 0, e, 2);
	#endif
			}else if(S_ISDIR(stbuf->st_mode)) {
				e->blkmap_size = NULL;
				e->blkmap_addr = NULL;
				e->blkmap_mem = NULL;
				e->blkmap_LRU = NULL;
				e->blkmap_continue = NULL;
				e->blkmap_offset = NULL;
				e->dir_entry = NULL;
				e->symbol_link=NULL;
				e->ra = NULL;
				pthread_rwlock_init(&(e->lock), NULL);

				/* local cache file */
				
				cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path));
				sprintf(cache_path, "%s%s",MFS_CACHE_PATH, path);
				res = mkdir(cache_path, 0777);
#ifdef MFS_DEBUG
				if(res == -1)
					printf("%s\n",(char *)strerror(errno));
#endif
				free(cache_path);
			}

		}
				HASH_ADD_STR(mfs_hash_table, path, e);
		
	}	
		if(e->attr_err < 0)
			return e->attr_err;
		else
			return 0;
	 	
}

static int mfs_readdir(const char *path, void *buf,
	       	fuse_fill_dir_t filler, off_t offset,
	       	struct fuse_file_info *fi)
{
	int res, net_res;
	int already_have;
	struct stat stbuf;
	struct mfs_hash_element *e=NULL;
	struct mfs_hash_blkmap *blk=NULL;
	int pack_len;
	pthread_t tid;
	struct mfs_task *task;
	char d_name[256];
	struct mfs_hash_dir_entry *dir_entry=NULL;

	(void) offset;
	(void) fi;

	HASH_FIND_STR(mfs_hash_table, path, e);
	if(e!= NULL){
#ifdef MFS_DEBUG
	printf("readdir hash got\n");
#endif

		if(e->dir_err < 0)
				return e->dir_err;
		if(e->dir_entry != NULL){
			dir_entry = e->dir_entry;
			while(dir_entry!=NULL){
				stbuf.st_ino = dir_entry->st_ino;
				stbuf.st_mode = dir_entry->st_mode;
				filler(buf, &(dir_entry->d_name), &stbuf, 0);
				dir_entry = dir_entry->next;
			}
			return 0;
		}
		already_have = 1;

	}

	if(e == NULL){
			already_have = 0;
			e = malloc(sizeof(struct mfs_hash_element) + mfs_strlen(path));
			strcpy(e->path, path);
			e->blkmap_size = NULL;
			e->blkmap_addr = NULL;
			e->blkmap_mem = NULL;
			e->blkmap_LRU = NULL;
			e->blkmap_continue = NULL;
			e->blkmap_offset = NULL;
			e->dir_entry = NULL;
			e->dir_err = 0;
			e->ra = NULL;
			e->symbol_link=NULL;
			pthread_rwlock_init(&(e->lock), NULL);
		
			task = get_mfs_task();
			if(task == NULL)
				return -ENOENT;
			net_res = send_mfs_command(task->connfd, MFS_GETATTR, mfs_strlen(path));
			if (net_res == -1){
				put_mfs_task(task);
				mfs_net_restart();
				free(e);
				return -ENOENT;
			}
			net_res = send_mfs_path_name(task->connfd, path);
			if (net_res == -1){
				put_mfs_task(task);
				mfs_net_restart();
				free(e);
				return -ENOENT;
			}
			net_res = get_mfs_reply(task->connfd, &res);
			if (net_res == -1){
				put_mfs_task(task);
				mfs_net_restart();
				free(e);
				return -ENOENT;
			}
	
			if(res == 0){ /*have following data package*/
				net_res = get_mfs_stat(task->connfd, &(e->attr));
				e->attr_err = 0;
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					free(e);
					return -ENOENT;
				}
			}else{
					e->attr_err = -res;
			}
			put_mfs_task(task);
	}
	
		task = get_mfs_task();
		if(task == NULL)
				return -ENOENT;
		
		net_res = send_mfs_command(task->connfd, MFS_READDIR, mfs_strlen(path));
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			return -ENOENT;
		}
		net_res = send_mfs_path_name(task->connfd, path);
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			return -ENOENT;
		}
		net_res = get_mfs_reply(task->connfd, &res);
	
		if(res == 0){//have following data
			pack_len = get_mfs_pack_info(task->connfd);
			if (pack_len == -1){
				put_mfs_task(task);
				mfs_net_restart();
				return -ENOENT;
			}
			if(already_have == 1)
				pthread_rwlock_wrlock(&(e->lock));
			e->dir_err = 0;
			while(pack_len>0){
				res = get_mfs_dir(task->connfd, pack_len, &stbuf.st_mode, &stbuf.st_ino, &d_name);
				if (res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					e->dir_err = -EAGAIN;
					pthread_rwlock_unlock(&(e->lock));
					return -ENOENT;
				}

				dir_entry = malloc(sizeof(struct mfs_hash_dir_entry));
				dir_entry->st_ino = stbuf.st_ino;
				dir_entry->st_mode = stbuf.st_mode;
				strcpy(dir_entry->d_name, d_name);
				dir_entry->next = e->dir_entry;
				e->dir_entry = dir_entry;
				e->dir_err++;
				filler(buf, &d_name, &stbuf, 0);
				pack_len = get_mfs_pack_info(task->connfd);
				if (pack_len == -1){
					put_mfs_task(task);
					mfs_net_restart();
					e->dir_err = -EAGAIN;
					pthread_rwlock_unlock(&(e->lock));
					return -ENOENT;
				}
			}
			if(already_have == 0){
				HASH_ADD_STR(mfs_hash_table, path, e);
			}else{
				pthread_rwlock_unlock(&(e->lock));

			}
	
		}else{
			if(already_have == 1)
				pthread_rwlock_wrlock(&(e->lock));
			e->dir_err = -res;
			if(already_have == 0){
				HASH_ADD_STR(mfs_hash_table, path, e);
			}else{
				pthread_rwlock_unlock(&(e->lock));
			}
			return -res;
		}
	
		put_mfs_task(task);
	
	return 0;
}

static int mfs_open(const char *path, struct fuse_file_info *fi)
{
	int res;
	int pbnum;
	char *pathbuf;
	struct mfs_hash_element *e;

/* LocalCheck */
	
	HASH_FIND_STR(mfs_hash_table, path, e);
	if(e == NULL)
			return -ENOENT;
	if(e!=NULL && e->attr_err ==0){
		return 0;
	}
	else
		return e->attr_err;
}

static int mfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct mfs_local_read *mlread;
	struct mfs_hash_element *e;
	struct mfs_task *task;
	char *cache_path;
	int fd;

	int net_res;
	int i=0;
	int j=0;
	int tmp_res;
	size_t blk_offset, blk_size;
	size_t remote_offset, remote_size;
	size_t local_offset, local_size;
	size_t save_size, save_offset;
	size_t hd_offset, hd_size;
	size_t res;
	struct readhead *ra;
	pthread_t tid;
	int lock_1,lock_2;
	int conn_res;
	int pack_len;
	char *buff_read;
	size_t tmp1,tmp2;
	char *test_buf;

#ifdef MFS_READ_DEBUG
	printf("read offset:%ju size:%zu\n",offset, size);
#endif

#ifndef MFS_HAVE_CACHE
	task = get_mfs_task();
	if(task == NULL)
		return -ENOENT;
	net_res = send_mfs_command(task->connfd, MFS_READ, mfs_strlen(path));	
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -ENOENT;
	}
	net_res = send_mfs_path_name(task->connfd, path);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -ENOENT;
	}
	net_res = send_mfs_read_info(task->connfd, offset, size);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -ENOENT;
	}
	net_res = get_mfs_reply(task->connfd, &conn_res);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -ENOENT;
	}
	pack_len = get_mfs_pack_info(task->connfd);
	if (pack_len == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -ENOENT;
	}

	net_res = get_mfs_read_buff(task->connfd, buf, pack_len);
	if (net_res == -1){
		put_mfs_task(task);
		mfs_net_restart();
		return -ENOENT;
	}
	put_mfs_task(task);
	return pack_len;
#endif

/* read_lock */
	watcher.cpu +=40;

	HASH_FIND_STR(mfs_hash_table, path, e);
	/*check if this offset is larger than read file*/
	if(e->blkmap_addr==NULL)
		return 0;

	blk_offset = offset / MFS_BLOCK_SIZE;
	blk_size = size / MFS_BLOCK_SIZE;
	remote_offset = 0;
	remote_size = 0;
	local_offset = 0;
	local_size = 0;
	hd_offset = 0;
	hd_size = 0;
	res = 0;
	pthread_rwlock_rdlock(&(e->lock));
	for(i = 0; i< blk_size; i++){
		if(e->blkmap_addr[blk_offset+i] == 0 && (path[strlen(path)-1] !='o' || path[strlen(path)-2]!='.')) {
			if(remote_size == 0)
				remote_offset =i * MFS_BLOCK_SIZE;

			remote_size +=MFS_BLOCK_SIZE;
			
		}
		else if(e->blkmap_addr[blk_offset+i] == 1) {
			if(local_size == 0)
				local_offset = i;
			if(e->blkmap_continue[blk_offset+local_offset+i]==1){/* still a continued mem block */
				local_size++;
			}else{/* get a 0, which means the end of this continued mem block, do read */
#ifdef MFS_DEBUG_CACHE
mfs_cache_hit++;
mfs_mem_cache_hit++;
#endif
				tmp1 = blk_offset+local_offset;
				tmp2 = (local_size) * MFS_BLOCK_SIZE + e->blkmap_size[blk_offset+local_offset+local_size];
				memcpy(buf + local_offset * MFS_BLOCK_SIZE, (char *)e->blkmap_mem[tmp1], tmp2);
				res+=tmp2;
				pthread_mutex_lock(&mfs_LRU_lock);
				if(e->blkmap_LRU[tmp1]!=NULL)
					update_mfs_LRU_element((struct mfsLRU *)e->blkmap_LRU[tmp1]);
				pthread_mutex_unlock(&mfs_LRU_lock);
#ifdef MFS_READ_DEBUG
				printf("get from mem, %zu\n", tmp2);
#endif
				local_size = 0;
			}
			if(remote_size!=0) {
#ifdef MFS_DEBUG_CACHE
mfs_cache_miss++;
#endif
				watcher.disk +=40;
				task = get_mfs_task();
				if(task == NULL)
					return -ENOENT;
				net_res = send_mfs_command(task->connfd, MFS_READ, mfs_strlen(path));	
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				net_res = send_mfs_path_name(task->connfd, path);
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				net_res = send_mfs_read_info(task->connfd, remote_offset+offset, remote_size);
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				net_res = get_mfs_reply(task->connfd, &conn_res);
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				if(conn_res == 0){
					pack_len = get_mfs_pack_info(task->connfd);
					if (pack_len == -1){
						put_mfs_task(task);
						mfs_net_restart();
						return -ENOENT;
					}
	
					net_res = get_mfs_read_buff(task->connfd, buf+remote_offset, pack_len);
					if (net_res == -1){
						put_mfs_task(task);
						mfs_net_restart();
						return -ENOENT;
					}
					res +=pack_len;
					tmp_res = pack_len;
					put_mfs_task(task);
	#ifdef MFS_HAVE_CACHE
					buff_read = malloc(pack_len);
					memcpy(buff_read, buf+remote_offset, pack_len);
					mfs_add_mem_cache(pack_len, remote_offset + offset, e, buff_read, 0, 0);
	#endif
	#ifdef MFS_READ_DEBUG
					printf("get from remote, %zu\n", pack_len);
	#endif
					remote_size = 0;
				}else {
					put_mfs_task(task);
				}
			}
#ifdef MFS_HAVE_DISK_CACHE
		}else if(e->blkmap_addr[blk_offset+i] >= 2) {
			if(hd_size == 0)
				hd_offset = i;
			if(e->blkmap_continue[blk_offset+i] == 1) {
				hd_size++;
			}else{
#ifdef MFS_DEBUG_CACHE
mfs_cache_hit++;
mfs_hd_cache_hit++;
#endif
				tmp1 = blk_offset+hd_offset;
				tmp2 = (hd_size) * MFS_BLOCK_SIZE + e->blkmap_size[blk_offset+hd_offset+hd_size];
#ifdef MFS_DEBUG
				printf("read from hd %ld at %ld, i=%d \n", tmp2, tmp1*MFS_BLOCK_SIZE, i);
#endif
				cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path) + 5);
				sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, path, tmp1 / MFS_DISK_CACHE_SPLIT_SIZE);
#ifdef MFS_DEBUG
				printf("read from %s\n", cache_path);
#endif
				fd = open(cache_path, O_RDONLY);
#ifdef MFS_DEBUG
				if(fd ==-1){
						printf("%s error,file %s, %s\n", __func__,cache_path, strerror(errno));
						return -ENOENT;
				}
#endif
				res += pread(fd, buf + hd_offset*MFS_BLOCK_SIZE, tmp2, (tmp1 % MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
				printf("%s read size%zu offset%zu\n", cache_path, tmp2, (tmp1 % MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
				close(fd);
				hd_size = 0;
				pthread_mutex_lock(&mfs_LRU_hd_lock);
				if(e->blkmap_LRU[tmp1]!=NULL)
					update_mfs_LRU_hd_element(cache_path);
				pthread_mutex_unlock(&mfs_LRU_hd_lock);
				free(cache_path);
			}
			if(remote_size!=0) {
#ifdef MFS_DEBUG_CACHE
mfs_cache_miss++;
#endif
				watcher.disk +=40;
				task = get_mfs_task();
				if(task == NULL)
					return -ENOENT;
				net_res = send_mfs_command(task->connfd, MFS_READ, mfs_strlen(path));	
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				net_res = send_mfs_path_name(task->connfd, path);
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				net_res = send_mfs_read_info(task->connfd, remote_offset+offset, remote_size);
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				net_res = get_mfs_reply(task->connfd, &conn_res);
				if (net_res == -1){
					put_mfs_task(task);
					mfs_net_restart();
					return -ENOENT;
				}
				if(conn_res == 0){
					pack_len = get_mfs_pack_info(task->connfd);
					if (pack_len == -1){
						put_mfs_task(task);
						mfs_net_restart();
						return -ENOENT;
					}
	
					net_res = get_mfs_read_buff(task->connfd, buf+remote_offset, pack_len);
					if (net_res == -1){
						put_mfs_task(task);
						mfs_net_restart();
						return -ENOENT;
					}
					res +=pack_len;
					tmp_res = pack_len;
					put_mfs_task(task);
	#ifdef MFS_HAVE_CACHE
					buff_read = malloc(pack_len);
					memcpy(buff_read, buf+remote_offset, pack_len);
					mfs_add_mem_cache(pack_len, remote_offset + offset, e, buff_read, 0, 0);
	#endif
	#ifdef MFS_READ_DEBUG
					printf("get from remote %d\n", pack_len);
	#endif
					remote_size = 0;
				}else {
					put_mfs_task(task);
				}
			}
#endif
		}

	}
#ifdef MFS_HAVE_DISK_CACHE
	if(hd_size !=0){
#ifdef MFS_DEBUG_CACHE
mfs_cache_hit++;
mfs_hd_cache_hit++;
#endif
		tmp1 = blk_offset+hd_offset;
		tmp2 = (hd_size-1) * MFS_BLOCK_SIZE + e->blkmap_size[blk_offset+hd_offset+hd_size-1];
#ifdef MFS_DEBUG
		printf("blk_offsett:%zu hd_offset: %zu hd_size:%zu e->blkmap_size:%d\n", blk_offset, hd_offset, hd_size, e->blkmap_size[blk_offset+hd_offset+hd_size-1]);
		printf("read from hd %ld from %ld \n", tmp2, tmp1*MFS_BLOCK_SIZE);
#endif
		cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path) + 5);
		sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, path, tmp1 / MFS_DISK_CACHE_SPLIT_SIZE);
		fd = open(cache_path, O_RDWR);
#ifdef MFS_DEBUG
		if(fd == -1){
				printf("%s error,file %s, %s\n", __func__,cache_path, strerror(errno));
		}
#endif
		res += pread(fd, buf + hd_offset*MFS_BLOCK_SIZE, tmp2, (tmp1 % MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
		close(fd);
		hd_size = 0;
		pthread_mutex_lock(&mfs_LRU_hd_lock);
#ifdef MFS_READ_DEBUG
				printf("get from disk, %zu\n", tmp2);
#endif
		if(e->blkmap_LRU[tmp1]!=NULL)
			update_mfs_LRU_hd_element(cache_path);
		pthread_mutex_unlock(&mfs_LRU_hd_lock);
		free(cache_path);
	}
#endif
#ifdef MFS_HAVE_CACHE
	if(local_size != 0){
#ifdef MFS_DEBUG_CACHE
mfs_cache_hit++;
mfs_mem_cache_hit++;
#endif
		tmp1 = blk_offset+local_offset;
		tmp2 = (local_size-1) * MFS_BLOCK_SIZE + e->blkmap_size[blk_offset+local_offset+local_size-1];
#ifdef MFS_DEBUG
		printf("tmp1=%d, tmp2=%d\n, offset=%jd, size=%zu\n, actual offset=%zu, actual size=%zu\n",
						tmp1, tmp2, offset, size, tmp1 * 4096, tmp2);
		printf("blkmap:%p\n", e->blkmap_mem[tmp1]);
#endif
		memcpy(buf + local_offset * MFS_BLOCK_SIZE, e->blkmap_mem[tmp1], tmp2);
		res+=tmp2;
		pthread_mutex_lock(&mfs_LRU_lock);
		if(e->blkmap_LRU[tmp1]!=NULL)
			update_mfs_LRU_element((struct mfsLRU *)e->blkmap_LRU[tmp1]);
		pthread_mutex_unlock(&mfs_LRU_lock);
		local_size = 0;

#ifdef MFS_READ_DEBUG
		printf("get from mem, %zu\n", tmp2);
#endif
	}
#endif
	pthread_rwlock_unlock(&(e->lock));
	if(remote_size != 0){
#ifdef MFS_DEBUG_CACHE
mfs_cache_miss++;
#endif
		watcher.disk +=40;
		task = get_mfs_task();
		if(task == NULL)
				return -ENOENT;
		net_res = send_mfs_command(task->connfd, MFS_READ, mfs_strlen(path));	
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			return -ENOENT;
		}
		net_res = send_mfs_path_name(task->connfd, path);
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			return -ENOENT;
		}
		net_res = send_mfs_read_info(task->connfd, remote_offset+offset, remote_size);
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			return -ENOENT;
		}
		net_res = get_mfs_reply(task->connfd, &conn_res);
		if (net_res == -1){
			put_mfs_task(task);
			mfs_net_restart();
			return -ENOENT;
		}
		if(conn_res == 0) {		
			pack_len = get_mfs_pack_info(task->connfd);
			if (pack_len== -1){
				put_mfs_task(task);
				mfs_net_restart();
				return -ENOENT;
			}
			printf("read size=%zu, offset=%zu, remote_offset=%zu packlen=%d\n", size, offset, remote_offset, pack_len);
			net_res = get_mfs_read_buff(task->connfd, buf+remote_offset, pack_len);
			if (net_res == -1){
				put_mfs_task(task);
				mfs_net_restart();
				return -ENOENT;
			}
			put_mfs_task(task);
			res +=pack_len;
			tmp_res = pack_len;
#ifdef MFS_READ_DEBUG
		printf("get from remote, %zu\n", pack_len);
#endif
#ifdef MFS_HAVE_CACHE
		buff_read = malloc(pack_len);
		memcpy(buff_read, buf+remote_offset, pack_len);
		mfs_add_mem_cache(pack_len, remote_offset + offset, e, buff_read, 0, 0);

#endif
		} else {
			put_mfs_task(task);		
		}
#ifdef MFS_HAVE_READHEAD
		/* readhead thread */
		if(offset + 131072 * 19 <= e->attr.st_size){
			mfs_add_readhead(131072, offset + 131072*18, e, 1);
			mfs_add_readhead(131072, offset + 131072*17, e, 1);
			mfs_add_readhead(131072, offset + 131072*16, e, 1);
			mfs_add_readhead(131072, offset + 131072*14, e, 1);
			mfs_add_readhead(131072, offset + 131072*13, e, 1);
			mfs_add_readhead(131072, offset + 131072*12, e, 1);
			mfs_add_readhead(131072, offset + 131072*11, e, 1);
			mfs_add_readhead(131072, offset + 131072*10, e, 1);
		}
#endif
	}
#ifdef MFS_DEBUG_CACHE
	printf("Mem Cache Hitted:%lu Disk Cache Hitted: %lu Cache total Hitted:%lu Cache Missed:%lu Hit/All %f\n",
				   	mfs_mem_cache_hit, mfs_hd_cache_hit, mfs_cache_hit, mfs_cache_miss, (float)mfs_cache_hit / (float)(mfs_cache_hit+mfs_cache_miss));
#endif
	return res;
}
static int mfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
		int fd;
		struct mfs_hash_element *e=NULL;
		size_t blk_size=0, alloc_blk_size;
		size_t blk_size_old=0;
		size_t save_offset, save_size;
		int res, j, test_res;
		int *blkmap_size=NULL;
		int *blkmap_addr=NULL;
		char **blkmap_mem=NULL;
		char *blkmap_continue=NULL;
		char **blkmap_LRU=NULL;
		char *cache_path=NULL;
		struct mfshdLRU *hd_name=NULL;
		struct mfsLRU *lru=NULL;
		char *test_buf=NULL;

		(void) fi;
		printf("write path %s size %zu offset %zu\n", path, size, offset);
		/* check if exist in hash map */
		HASH_FIND_STR(mfs_hash_table, path, e);

		if(e==NULL){/* no such file */
			/* create file */
				e = malloc(sizeof(struct mfs_hash_element) + mfs_strlen(path));
				cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path) + 5);
				sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, path, offset / (MFS_DISK_CACHE_SPLIT_SIZE * MFS_BLOCK_SIZE));
				fd = open(cache_path, O_CREAT|O_RDWR, 0666);
				if(fd == -1){/* create erro */
					res = -errno;
					free(cache_path);
					free(e);
					return res;
				}

				lstat(cache_path, &(e->attr));
				e->attr_err = 0;

			/* create blkmap */
				blk_size = (offset + size) / MFS_BLOCK_SIZE;
				if((offset + size) % MFS_BLOCK_SIZE > 0)
						blk_size++;

				e->blkmap_size = calloc(blk_size, sizeof(int));
				memset(e->blkmap_size, 0, sizeof(int) * blk_size);

				e->blkmap_addr = calloc(blk_size, sizeof(int));
				memset(e->blkmap_addr, 0, sizeof(int) * blk_size);

				e->blkmap_mem = calloc(blk_size, sizeof(char *));

				e->blkmap_continue = calloc(blk_size, sizeof(char));
				memset(e->blkmap_continue, 0, sizeof(char) * blk_size);

				e->blkmap_LRU = calloc(blk_size, sizeof(char *));
				memset(e->blkmap_LRU, 0, sizeof(char *) * blk_size);
				pthread_rwlock_init(&(e->lock), NULL);
				e->dir_entry = NULL;
				e->ra = malloc(sizeof(struct readhead));
				((struct readhead *)e->ra)->e = e;
				((struct readhead *)e->ra)->offset = 0;
				((struct readhead *)e->ra)->size = 131072;
				((struct readhead *)e->ra)->next = NULL;
			/* write file */
				res = pwrite(fd, buf, size, offset);
				close(fd);
				free(cache_path);
			/* update blkmap */
				/* no need to lock this operation because it does not in the hash table */
				save_size = res / MFS_BLOCK_SIZE;
				save_offset = offset / MFS_BLOCK_SIZE;
				for(j=0;j<save_size;j++) {
					e->blkmap_size[save_offset + j] = MFS_BLOCK_SIZE;
					e->blkmap_addr[save_offset + j] = 3;
					e->blkmap_continue[save_offset+j] = 1;
				}
				if(res % MFS_BLOCK_SIZE !=0) {
					e->blkmap_size[save_offset + j]= res % MFS_BLOCK_SIZE;
					e->blkmap_addr[save_offset + j] = 3;
					e->blkmap_continue[save_offset+j] = 0;
				}else{
					e->blkmap_continue[save_offset+j-1] = 0;
				}

				HASH_ADD_STR(mfs_hash_table, path, e);
			add_mfs_upload_task(offset, size, e);
				return res;
		}else{/* have this file */

				blk_size = (offset + size) / MFS_BLOCK_SIZE;
				save_offset = offset / MFS_BLOCK_SIZE;
				save_size = size / MFS_BLOCK_SIZE;
				if(size % MFS_BLOCK_SIZE !=0)
						save_size++;
				/* update blkmap */
				pthread_rwlock_wrlock(&(e->lock));
				if(e->attr.st_size < offset + size){/*need truncate blkmap*/
					if((offset + size) % MFS_BLOCK_SIZE > 0) {
							alloc_blk_size = blk_size+2;
					}else {
							alloc_blk_size = blk_size+1;
					}

					blk_size_old = (e->attr.st_size)/MFS_BLOCK_SIZE;
					if((e->attr.st_size)%MFS_BLOCK_SIZE > 0)
							blk_size_old++;

					blkmap_size = malloc(sizeof(int) * alloc_blk_size);
					memset(blkmap_size, 0, sizeof(int) * (alloc_blk_size));
					if(e->blkmap_size!=NULL){
						memcpy(blkmap_size, e->blkmap_size, sizeof(int) * blk_size_old);
						free(e->blkmap_size);
					}
					e->blkmap_size = blkmap_size;

					blkmap_addr = malloc(sizeof(int) * alloc_blk_size);
					memset(blkmap_addr, 0, sizeof(int) * (alloc_blk_size));
					if(e->blkmap_addr!=NULL){
						memcpy(blkmap_addr, e->blkmap_addr, sizeof(int) * blk_size_old);
						free(e->blkmap_addr);
					}
					e->blkmap_addr = blkmap_addr;

					blkmap_mem = malloc(sizeof(char *) * alloc_blk_size);
					memset(blkmap_mem, 0, sizeof(char *) * (alloc_blk_size));
					if(e->blkmap_mem!=NULL){
						memcpy(blkmap_mem, e->blkmap_mem, sizeof(char *) * blk_size_old);
						free(e->blkmap_mem);
					}
					e->blkmap_mem = blkmap_mem;

					blkmap_continue = malloc(sizeof(char) * alloc_blk_size);
					memset(blkmap_continue, 0, sizeof(char) * (alloc_blk_size));
					if(e->blkmap_continue!=NULL){
						memcpy(blkmap_continue, e->blkmap_continue, sizeof(char) * blk_size_old);
						free(e->blkmap_continue);
					}
					e->blkmap_continue= blkmap_continue;

					blkmap_LRU = malloc(sizeof(char *) * alloc_blk_size);
					memset(blkmap_LRU, 0, sizeof(char *) * (alloc_blk_size));
					if(e->blkmap_LRU!=NULL){
						memcpy(blkmap_LRU, e->blkmap_LRU, sizeof(char *) * blk_size_old);
						free(e->blkmap_LRU);
					}
					e->blkmap_LRU = blkmap_LRU;
					e->attr.st_size = offset + size;/*change file size*/
				}
				pthread_rwlock_unlock(&(e->lock));
				/* write this file to disk */
				cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path) + 5);
				sprintf(cache_path, "%s%s-%lu", MFS_CACHE_PATH, path, offset / (MFS_DISK_CACHE_SPLIT_SIZE * MFS_BLOCK_SIZE));
				fd = open(cache_path, O_CREAT|O_RDWR, 0666);
#ifdef MFS_DEBUG
				printf("write to %s\n", cache_path);
#endif 
				if(fd == -1)
					printf("write %s open failed, erro:%s\n", cache_path, (char *)strerror(errno));
				/* fix data errors, if offset and size is not divided by MFS_BLOCK_SIZE, then deal with datas in memories */
				pthread_rwlock_wrlock(&(e->lock));
				if(e->blkmap_addr[offset / MFS_BLOCK_SIZE] == 1) {
					lru = e->blkmap_LRU[blk_size];
					pwrite(fd , lru->addr, lru->len,
						(lru->index % MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
				}

				res = pwrite(fd, buf, size, offset - ((offset / MFS_BLOCK_SIZE) / MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
				printf("write path %s size %zu offset %zu\n", cache_path, size, offset - ((offset / MFS_BLOCK_SIZE) / MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);

				if(res != size){
					printf("write error\n");
					printf("%s\n", (char *)strerror(errno));
				}
				close(fd);
				free(cache_path);

				if(lru != NULL)
					for(j=0;j<lru->size;j++) {
						e->blkmap_addr[lru->index + j] = 3;
					}
				else {
					for(j=0;j<save_size;j++) {
						e->blkmap_addr[save_offset+j] = 3;
						e->blkmap_size[save_offset + j]= MFS_BLOCK_SIZE;
						e->blkmap_continue[save_offset+j] = 1;
					}
						if(save_offset+j!=0)
							e->blkmap_continue[save_offset+j-1] = 0;
						else
							e->blkmap_continue[save_offset+j] = 0;

				}
				pthread_rwlock_unlock(&(e->lock));

            	add_mfs_upload_task(offset, size, e);
				return res;
		}

}
static int mfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
		int res;
		struct mfs_hash_element *e=NULL, *dir=NULL;
		struct mfs_hash_dir_entry *dir_entry;
		char *cache_path;
		char *last_slash, *last_slash_left;
		char *dir_path;
		int already_have = 0;
		int i;


		/* check existance */
		HASH_FIND_STR(mfs_hash_table, path, e);
		if(e!=NULL && e->attr_err!=-2){/* other errors except no such file error */
				return e->attr_err;
		}else if(e!=NULL && e->attr_err == -2){
				already_have = 1;
		}
				/* need to create this */
		if(already_have == 0){
				e = malloc(sizeof(struct mfs_hash_element) + mfs_strlen(path));
		}
				cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path));
				sprintf(cache_path, "%s%s",MFS_CACHE_PATH, path);
				if(!S_ISREG(mode)){/* currently just support regular file */
						free(cache_path);
						return -EINVAL;
				}
				res = mknod(cache_path, mode, rdev);
				if(res == -1){/* create erro */
					printf("%s error:%s file:%s\n",__func__, (char *)strerror(errno), cache_path);
					res = -errno;
					free(cache_path);
					if(already_have==0)
						free(e);
					return res;
				}

		if(already_have == 1)
				pthread_rwlock_wrlock(&(e->lock));

		lstat(cache_path, &(e->attr));
		e->attr_err = 0;

		if(already_have == 1)
				pthread_rwlock_unlock(&(e->lock));
		
		free(cache_path);
		
		if(already_have == 0){
				e->blkmap_size=NULL ;
				e->blkmap_addr = NULL;
				e->blkmap_mem = NULL;
				e->blkmap_continue = NULL;
				e->blkmap_LRU = NULL;
				pthread_rwlock_init(&(e->lock), NULL);
				e->dir_entry = NULL;
				e->symbol_link = NULL;
				e->ra = malloc(sizeof(struct readhead));
				((struct readhead *)e->ra)->e = e;
				((struct readhead *)e->ra)->offset = 0;
				((struct readhead *)e->ra)->size = 131072;
				((struct readhead *)e->ra)->next = NULL;
				HASH_ADD_STR(mfs_hash_table, path, e);
		}
				/* add to dirctory */
				for(i=strlen(path)-1;i>=0;i--)
					if(path[i] == '/')
						break;
				if(i == 0){/* at root */
					dir_path = malloc(sizeof(char)*2);
					strcpy(dir_path, "/\0");
				}else{/* not root */
					dir_path = malloc(i+1);
					strncpy(dir_path, path, i);
					dir_path[i] = '\0';
				}
				HASH_FIND_STR(mfs_hash_table, dir_path, dir);
				if(dir==NULL){
#ifdef MFS_DEBUG
						printf("serach containing directory failed, dir is %s\n", dir_path);
#endif
						free(dir_path);
						return -ENOENT;
				}else{
					dir_entry = malloc(sizeof(struct mfs_hash_dir_entry));
					dir_entry->st_ino = e->attr.st_ino;
					dir_entry->st_mode = e->attr.st_mode;
					strcpy(dir_entry->d_name, (char *)(path+i+1));
					dir_entry->next = NULL;
#ifdef MFS_DEBUG
					printf("add a dir entry, d_name is %s \n", dir_entry->d_name);
#endif
					pthread_rwlock_wrlock(&(dir->lock));
					dir_entry->next = dir->dir_entry;
					dir->dir_entry = dir_entry;
					dir->dir_err++;
					pthread_rwlock_unlock(&(dir->lock));
				}
				free(dir_path);
				return 0;
}
static int mfs_truncate(const char *path, off_t size)
{
		int res;
		struct mfs_hash_element *e;
		size_t diff_size;
		size_t blk_size, target_size, blk_size_cpy;
		int *blkmap_size;
		int *blkmap_addr;
		char **blkmap_mem;
		char *blkmap_continue;
		char **blkmap_LRU;

		HASH_FIND_STR(mfs_hash_table, path, e);

		if(e==NULL)
				return -ENOENT;
		
		pthread_rwlock_wrlock(&(e->lock));

		if(size == 0 && e->attr.st_size!=0) {
				e->attr.st_size=NULL;
				e->blkmap_addr=NULL;
				e->blkmap_continue=NULL;
				e->blkmap_LRU=NULL;
				e->blkmap_mem=NULL;
				e->symbol_link=NULL;
				pthread_rwlock_unlock(&(e->lock));
				return 0;
		}

		if (size > e->attr.st_size)
				target_size = e->attr.st_size;
		else if(size < e->attr.st_size)
				target_size = size;
		else{
			pthread_rwlock_unlock(&(e->lock));
			return 0;
		}
		
		blk_size = (size) / MFS_BLOCK_SIZE+1;
		if(size % MFS_BLOCK_SIZE > 0)
			blk_size++;

		blk_size_cpy = (target_size) / MFS_BLOCK_SIZE;
		if(target_size%MFS_BLOCK_SIZE > 0)
			blk_size_cpy++;

		e->attr.st_size = size;

		blkmap_size = malloc(sizeof(int) * blk_size);
		memset(blkmap_size, 0, sizeof(int) * (blk_size));
		if(e->blkmap_size!=NULL){
			memcpy(blkmap_size, e->blkmap_size, sizeof(int) * blk_size_cpy);
		}
		e->blkmap_size = blkmap_size;

		blkmap_addr = malloc(sizeof(int) * blk_size);
		memset(blkmap_addr, 0, sizeof(int) * (blk_size));
		if(e->blkmap_addr!=NULL){
			memcpy(blkmap_addr, e->blkmap_addr, sizeof(int) * blk_size_cpy);
		}
		e->blkmap_addr = blkmap_addr;

		blkmap_mem = malloc(sizeof(char *) * blk_size);
		memset(blkmap_mem, 0, sizeof(char *) * (blk_size));
		if(e->blkmap_mem!=NULL){
			memcpy(blkmap_mem, e->blkmap_mem, sizeof(char *) * blk_size_cpy);
		}
		e->blkmap_mem = blkmap_mem;

		blkmap_continue = malloc(sizeof(char) * blk_size);
		memset(blkmap_continue, 0, sizeof(char) * (blk_size));
		if(e->blkmap_continue!=NULL){
			memcpy(blkmap_continue, e->blkmap_continue, sizeof(char) * blk_size_cpy);
		}
		e->blkmap_continue = blkmap_continue;

		blkmap_LRU = malloc(sizeof(char *) * blk_size);
		memset(blkmap_LRU, 0, sizeof(char *) * (blk_size));
		if(e->blkmap_LRU!=NULL){
			memcpy(blkmap_LRU, e->blkmap_LRU, sizeof(char *) * blk_size_cpy);
		}
		e->blkmap_LRU= blkmap_LRU;
		pthread_rwlock_unlock(&(e->lock));
		return 0;

}

static int mfs_mkdir(const char *path, mode_t mode)
{
		struct mfs_hash_element *dir, *e;
		struct mfs_hash_dir_entry *dir_entry;
		int res;
		char *cache_path, *dir_path;
		int i;

		HASH_FIND_STR(mfs_hash_table, path, e);
		if(e == NULL){
				printf("%s no getattr before\n", __func__);
				return -ENOENT;
		}
		
		cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path));
		sprintf(cache_path, "%s%s",MFS_CACHE_PATH, path);
		res = mkdir(cache_path, mode);
		if(res != 0 && res != -EEXIST)
			return res;	
		pthread_rwlock_wrlock(&(e->lock));
		lstat(cache_path, &(e->attr));
		e->attr_err = 0;
		e->blkmap_size = NULL;
		e->blkmap_addr = NULL;
		e->blkmap_mem = NULL;
		e->blkmap_LRU = NULL;
		e->blkmap_continue = NULL;
		e->dir_entry = NULL;
		e->symbol_link = NULL;
		pthread_rwlock_unlock(&(e->lock));
		free(cache_path);

				/* add to dirctory */
				for(i=strlen(path)-1;i>=0;i--)
					if(path[i] == '/')
						break;
				if(i == 0){/* at root */
					dir_path = malloc(sizeof(char)*2);
					strcpy(dir_path, "/\0");
				}else{/* not root */
					dir_path = malloc(i+1);
					strncpy(dir_path, path, i);
					dir_path[i] = '\0';
				}
				HASH_FIND_STR(mfs_hash_table, dir_path, dir);
					dir_entry = malloc(sizeof(struct mfs_hash_dir_entry));
					dir_entry->st_ino = e->attr.st_ino;
					dir_entry->st_mode = e->attr.st_mode;
					strcpy(dir_entry->d_name, (char *)(path+i+1));
					dir_entry->next = NULL;
#ifdef MFS_DEBUG
					printf("add a dir entry, d_name is %s \n", dir_entry->d_name);
#endif
					pthread_rwlock_wrlock(&(dir->lock));
					dir_entry->next = dir->dir_entry;
					dir->dir_entry = dir_entry;
					dir->dir_err++;
					pthread_rwlock_unlock(&(dir->lock));

		return 0;
}

static int mfs_unlink(const char *path)
{
	struct mfs_hash_element *dir, *e;
	char *dir_path;
	struct mfs_hash_dir_entry *dir_entry, *dir_entry_tmp;
	int i;
	char *cache_path;

	/* find dirctory */
	for(i=strlen(path)-1;i>=0;i--)
		if(path[i] == '/')
			break;
	if(i == 0){/* at root */
		dir_path = malloc(sizeof(char)*2);
		strcpy(dir_path, "/\0");
	}else{/* not root */
		dir_path = malloc(i+1);
		strncpy(dir_path, path, i);
		dir_path[i] = '\0';
	}
	
	HASH_FIND_STR(mfs_hash_table, dir_path, dir);

	pthread_rwlock_wrlock(&(dir->lock));
	dir_entry = dir->dir_entry;
	if(dir_entry!=NULL)
	if(strcmp(dir_entry->d_name, (char *)(path+i+1)) == 0){
		dir->dir_entry = dir_entry->next;
	}else {
		while(dir_entry->next!=NULL){
			if(strcmp(dir_entry->next->d_name, (char *)(path+i+1)) == 0) {
				dir_entry_tmp = dir_entry->next->next;
				dir_entry->next = dir_entry_tmp;
				dir->dir_err--;
				break;
			}
			dir_entry = dir_entry->next;
		}
	}
	pthread_rwlock_unlock(&(dir->lock));

	HASH_FIND_STR(mfs_hash_table, path, e);
	if(e!=NULL){
		pthread_rwlock_wrlock(&(e->lock));
		if(strcmp(e->path, path) == 0)
			HASH_DELETE(hh, mfs_hash_table, e);
		pthread_rwlock_unlock(&(e->lock));
	}
	free(dir_path);

	cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path));
	sprintf(cache_path, "%s%s",MFS_CACHE_PATH, path);
	remove(cache_path);//delete this big cache block
	free(cache_path);

	return 0;
}

static int mfs_chmod(const char *path, mode_t mode)
{
		struct mfs_hash_element *e;
		HASH_FIND_STR(mfs_hash_table, path, e);
		if(e==NULL)
				return -ENOENT;
		if(e->attr_err == -2)
				return e->attr_err;
		
		pthread_rwlock_wrlock(&(e->lock));
		e->attr.st_mode = mode;
		pthread_rwlock_unlock(&(e->lock));

		return 0;
}

static int mfs_rmdir(const char *path)
{
		struct mfs_hash_element *e, *dir;
		struct mfs_hash_dir_entry *dir_entry, *dir_entry_tmp;
		int count=0;
		char *dir_path;
		int i;
		HASH_FIND_STR(mfs_hash_table, path, e);

		dir_entry = e->dir_entry;
		while(dir_entry!=NULL){
				if(strcmp(dir_entry->d_name, ".")!=0 && strcmp(dir_entry->d_name, "..")!=0)
						count = 1;
				dir_entry = dir_entry->next;
		}

		if(count == 1)
				return -ENOTEMPTY;

	/* find dirctory */
	for(i=strlen(path)-1;i>=0;i--)
		if(path[i] == '/')
			break;
	if(i == 0){/* at root */
		dir_path = malloc(sizeof(char)*2);
		strcpy(dir_path, "/\0");
	}else{/* not root */
		dir_path = malloc(i+1);
		strncpy(dir_path, path, i);
		dir_path[i] = '\0';
	}
	
	HASH_FIND_STR(mfs_hash_table, dir_path, dir);

		pthread_rwlock_wrlock(&(dir->lock));
		dir_entry = dir->dir_entry;
		if(strcmp(dir_entry->d_name, (char *)(path+i+1)) == 0){
			dir->dir_entry = dir_entry->next;
		}else {
			while(dir_entry->next!=NULL){
				if(strcmp(dir_entry->next->d_name, (char *)(path+i+1))== 0) {
					dir_entry_tmp = dir_entry->next->next;
					dir_entry->next = dir_entry_tmp;
					dir->dir_err--;
					break;
				}
				dir_entry = dir_entry->next;
			}
		}
		pthread_rwlock_unlock(&(dir->lock));
		free(dir_path);
		HASH_DELETE(hh, mfs_hash_table, e);
		return 0;
}

static int mfs_utimens(const char *path, const struct timespec ts[2])
{
		int res;
		struct mfs_hash_element *e;
		struct timeval tv[2];

		tv[0].tv_sec = ts[0].tv_sec;
		tv[0].tv_usec = ts[0].tv_nsec / 1000;
		tv[1].tv_sec = ts[1].tv_sec;
		tv[1].tv_usec = ts[1].tv_nsec / 1000;


		HASH_FIND_STR(mfs_hash_table, path, e);

		if(e == NULL || e->attr_err != 0)
				return -ENOENT;
		pthread_rwlock_wrlock(&(e->lock));
		/* not a good implementation, lost usec */
		e->attr.st_atime = tv[0].tv_sec;
		e->attr.st_mtime = tv[1].tv_sec;
		pthread_rwlock_unlock(&(e->lock));

		return 0;
}

static int mfs_rename(const char *from, const char *to)
{
	int res;
	int i;
	int have=0;
	struct mfs_hash_element *from_e, *to_e, *tmp_from_e, *dir;
	char *cache_path_from, *cache_path_to;
	char *dir_path;
	struct mfs_hash_dir_entry *dir_entry, *tmp_entry;

	HASH_FIND_STR(mfs_hash_table, from, from_e);
	HASH_DELETE(hh, mfs_hash_table, from_e);
	HASH_FIND_STR(mfs_hash_table, to, to_e);
	HASH_DELETE(hh, mfs_hash_table, to_e);

	pthread_rwlock_wrlock(&(from_e->lock));
	pthread_rwlock_wrlock(&(to_e->lock));

	strcpy(from_e->path, to);

	cache_path_from = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(from) + 5);
	cache_path_to = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(to) + 5);
	for(i=0;i<=from_e->attr.st_size / (MFS_DISK_CACHE_SPLIT_SIZE * MFS_BLOCK_SIZE); i++){
		sprintf(cache_path_from, "%s%s-%lu",MFS_CACHE_PATH, from, (MFS_DISK_CACHE_SPLIT_SIZE * MFS_BLOCK_SIZE) * i);
		sprintf(cache_path_to, "%s%s-%lu",MFS_CACHE_PATH, to, (MFS_DISK_CACHE_SPLIT_SIZE * MFS_BLOCK_SIZE) * i);
		res = rename(cache_path_from, cache_path_to);
	}

	HASH_ADD_STR(mfs_hash_table, path, from_e);
	pthread_rwlock_unlock(&(from_e->lock));
	pthread_rwlock_unlock(&(to_e->lock));

	/* find dirctory */
	for(i=strlen(to)-1;i>=0;i--)
		if(to[i] == '/')
			break;
	if(i == 0){/* at root */
		dir_path = malloc(sizeof(char)*2);
		strcpy(dir_path, "/\0");
	}else{/* not root */
		dir_path = malloc(i+1);
		strncpy(dir_path, to, i);
		dir_path[i] = '\0';
	}
	
	HASH_FIND_STR(mfs_hash_table, dir_path, dir);

		pthread_rwlock_wrlock(&(dir->lock));
		dir_entry = dir->dir_entry;
		while(dir_entry->next!=NULL){
			if(strcmp(dir_entry->d_name, (char *)(to+i+1))== 0) {
					have = 1;
				break;
			}
			dir_entry = dir_entry->next;
		}
		if(have == 0) {
			tmp_entry = malloc(sizeof(struct mfs_hash_dir_entry));
			tmp_entry->st_ino = from_e->attr.st_ino;
			tmp_entry->st_mode = from_e->attr.st_mode;
			strcpy(tmp_entry->d_name, (char *)(to+i+1));
			tmp_entry->next = NULL;	
			dir_entry->next = tmp_entry;
			dir->dir_err++;
		}
		pthread_rwlock_unlock(&(dir->lock));
		free(dir_path);
	return 0;
}

static int mfs_link(const char *from, const char *to)
{
	return 0;

}

static int mfs_symlink(const char *from, const char *to)
{
		int res;
		struct mfs_hash_element *from_e, *to_e, *e;
		char *cache_path;

		/* overwrite file */
		HASH_FIND_STR(mfs_hash_table, to, to_e);
		if(to_e != NULL)
			HASH_DELETE(hh, mfs_hash_table, to_e);

		HASH_FIND_STR(mfs_hash_table, from, from_e);

		e = malloc(sizeof(struct mfs_hash_element) + strlen(to)+1);

		strcpy(e->path, to);
		e->blkmap_addr = NULL;
		e->blkmap_size = NULL;
		e->blkmap_continue = NULL;
		e->blkmap_mem = NULL;
		e->blkmap_LRU = NULL;
		pthread_mutex_init(&(e->lock), NULL);
		e->dir_entry = NULL;
		e->symbol_link = malloc(strlen(from)+1);
		strcpy(e->symbol_link, from);
		cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(to));
		sprintf(cache_path, "%s%s",MFS_CACHE_PATH, to);
		res = symlink(from, cache_path);/* create cache_path which is a symbol to from */
		e->attr_err = lstat(cache_path, &(e->attr));
		if(e->attr_err == -1)
				e->attr_err = -errno;
		HASH_ADD_STR(mfs_hash_table, path, e);
		free(cache_path);
		return 0;
}

static int mfs_readlink(const char *path, char *buf, size_t size)
{
	int res;
	struct mfs_hash_element *e;
	char *cache_path;
	HASH_FIND_STR(mfs_hash_table, path, e);
	cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(path));
	sprintf(cache_path, "%s%s",MFS_CACHE_PATH, path);
	res = readlink(cache_path, buf, size - 1);
	free(cache_path);
	if(res == -1)
			return -errno;
	buf[res] = '\0';
	return 0;
}

static struct fuse_operations mfs_oper = {

	.getattr = mfs_getattr,
	.readdir = mfs_readdir,
	.open    = mfs_open,
  	.read    = mfs_read,
	
	.write   = mfs_write,
	.mknod   = mfs_mknod,
	.truncate= mfs_truncate,
	.unlink  = mfs_unlink,
	.chmod 	 = mfs_chmod,	
	.rmdir   = mfs_rmdir,
	.utimens = mfs_utimens,
	.rename  = mfs_rename,
	.link	 = mfs_link,
	.symlink = mfs_symlink,
	.readlink= mfs_readlink,
	.mkdir   = mfs_mkdir,
};

void mfs_cleanup_LRU()
{
		struct mfsLRU *lru, *tmp_lru;
		char *cache_path;
		int fd;
		int i, res;

		pthread_mutex_lock(&mfs_LRU_lock);
		DL_FOREACH_SAFE(mfs_LRU_list, lru, tmp_lru){
			cache_path = malloc(MFS_CACHE_PATH_LEN + mfs_strlen(lru->e->path) + 5);
			sprintf(cache_path, "%s%s-%lu",MFS_CACHE_PATH, lru->e->path, lru->index / MFS_DISK_CACHE_SPLIT_SIZE);
			fd = open(cache_path, O_CREAT|O_RDWR, 0777);
			if(fd != -1){
					res = pwrite(fd, lru->addr, lru->len, (lru->index% MFS_DISK_CACHE_SPLIT_SIZE) * MFS_BLOCK_SIZE);
#ifdef MFS_DEBUG
					if(res == lru->index*MFS_BLOCK_SIZE)
							printf("write %s ok\n", cache_path);
#endif
					close(fd);
					for(i=0;i<lru->size;i++)
							lru->e->blkmap_addr[lru->index+i]=2;
			}else{
					memset(&lru->e->blkmap_addr[lru->index], 0, sizeof(int)* (lru->size));
					memset(&lru->e->blkmap_continue[lru->index], 0, sizeof(char) * (lru->size));
			}
			free(lru->addr);
			free(cache_path);
			free(lru);
		}
		pthread_mutex_unlock(&mfs_LRU_lock);

}

void mfs_cleanup_file_element()
{
		struct mfs_hash_element *e, *tmp_e;
		struct mfs_hash_dir_entry *dir_entry, *tmp_dir;
		size_t ptr=0;
		size_t size=0;
		short type;
		size_t size_st = sizeof(size_t);
		int fd;
		int i;
		int res;
		size_t blk_size;

		fd = open(MFS_CACHE_FILE, O_CREAT|O_RDWR, 0644);
#ifdef MFS_DEBUG
		if(fd == -1)
				printf("can not write map file, error:%s\n", (char *)strerror(errno));
#endif

	pthread_mutex_lock(&mfs_LRU_lock);
		HASH_ITER(hh, mfs_hash_table, e, tmp_e){
#ifdef MFS_DEBUG
			printf("clean %s\n", e->path);
#endif
			if(e == NULL)
					break;

			size = sizeof(*e) + mfs_strlen(e->path);
			
			res = pwrite(fd, &size, size_st, ptr);
#ifdef MFS_DEBUG
			printf("write size\n");
#endif
			ptr +=res;
			
			res = pwrite(fd, e, size, ptr);
#ifdef MFS_DEBUG
			printf("write e\n");
#endif
			ptr +=res;
			
			if(e->blkmap_addr != NULL){
					type = 0;/* normal file */
			}else if(e->dir_entry != NULL){
					type = 1;/* dirent file */
			}else{
					type = 2;
			}

			res = pwrite(fd, &type, sizeof(short), ptr);
#ifdef MFS_DEBUG
			printf("write type\n");
#endif
			ptr +=res;
	
			if(type == 0){

				blk_size = e->attr.st_size / MFS_BLOCK_SIZE;
				if(e->attr.st_size % MFS_BLOCK_SIZE!= 0)
						blk_size++;
				res = pwrite(fd, e->blkmap_addr, blk_size * sizeof(int), ptr);
#ifdef MFS_DEBUG
				printf("write addr\n");
#endif
				ptr +=res;
	
				res = pwrite(fd, e->blkmap_size, blk_size * sizeof(int), ptr);
#ifdef MFS_DEBUG
			printf("write blksize\n");
#endif
				ptr +=res;
	
				res = pwrite(fd, e->blkmap_continue, blk_size * sizeof(char), ptr);
#ifdef MFS_DEBUG
			printf("write continue\n");
#endif
				ptr +=res;
				
				free(e->blkmap_addr);
				free(e->blkmap_size);
				free(e->blkmap_continue);
				free(e->blkmap_mem);
				free(e->blkmap_LRU);

			}else if(type == 1){

				dir_entry = e->dir_entry;
				size = sizeof(struct mfs_hash_dir_entry);
#ifdef MFS_DEBUG
				printf("sizeof mfs_hash_dir_entry %d, number %d\n", size, e->dir_err);
#endif
				while(dir_entry != NULL){
						res = pwrite(fd, dir_entry, size, ptr);
#ifdef MFS_DEBUG
						printf("write dir_entry\n");
#endif
						ptr +=res;
						tmp_dir = dir_entry;
						dir_entry = dir_entry->next;
						free(tmp_dir);
				}
			}

			free(e);
#ifdef MFS_DEBUG
			printf("ptr is %d\n", ptr);
#endif
		}
		
		if(fd != -1)
			close(fd);

}
void mfs_cleanup()
{
		struct readhead *ra;
		struct memcache *mcache;
		char *cache_path;
		

		/* cleanup memory cache */
		mfs_cleanup_LRU();
#ifdef MFS_DEBUG
		printf("LRU cleaned up\n");
#endif
		/* save and clean mfs_hash_table */	
		mfs_cleanup_file_element();
#ifdef MFS_DEBUG
		printf("file element cleaned up\n");
#endif
		/* cleanup ra list */
		while(ra_list_seq_head!=NULL) {
				ra = ra_list_seq_head;
				ra_list_seq_head = ra->next;
				free(ra);
		}
		while(ra_list != NULL){
				ra = ra_list;
				ra_list = ra->next;
				free(ra);
		}

		/* cleanup memcache */
		while(mem_list!=NULL){
				mcache = mem_list;
				mem_list = mcache->next;
				free(mcache);
		}
		while(mem_list_ra!=NULL){
				mcache = mem_list_ra;
				mem_list_ra = mcache->next;
				free(mcache);
		}

}
void mfs_load_file_element()
{
		struct mfs_hash_element *e, *tmp_e;
		struct mfs_hash_dir_entry *dir_entry, *tmp_dir;
		size_t ptr=0;
		size_t size=0;
		short type;
		size_t size_st = sizeof(size_t);
		int fd;
		int i;
		int res;
		size_t blk_size;

		fd = open(MFS_CACHE_FILE, O_RDONLY);

		if(fd == -1){
#ifdef MFS_DEBUG
				printf("No cache file existed\n");
#endif
				return;
		}
		while(1){
			res = pread(fd, &size, sizeof(size_t), ptr);
			if(res <= 0)
					return;
			ptr +=res;
			
			e = malloc(size);
			
			res = pread(fd, e, size, ptr);
			ptr +=res;

#ifdef MFS_DEBUG
			printf("get %s\n", e->path);
#endif

			res = pread(fd, &type, sizeof(short), ptr);
			ptr +=res;

			switch(type){
					case 0:
							blk_size = e->attr.st_size / MFS_BLOCK_SIZE;
							if(e->attr.st_size % MFS_BLOCK_SIZE!= 0)
								blk_size++;
#ifdef MFS_DEBUG
							printf("size is %d\n", blk_size * sizeof(int));
#endif
							e->blkmap_addr = malloc(blk_size * sizeof(int));
							e->blkmap_size = malloc(blk_size * sizeof(int));
							e->blkmap_continue = malloc(blk_size * sizeof(char));
							e->blkmap_mem = malloc(sizeof(char *) * blk_size);
							e->blkmap_LRU  = malloc(blk_size * sizeof(char *));
							memset(e->blkmap_LRU, 0, sizeof(char *) * blk_size);
							pthread_rwlock_init(&(e->lock), NULL);
							res = pread(fd, e->blkmap_addr, blk_size * sizeof(int), ptr);
							ptr += res;
							res = pread(fd, e->blkmap_size, blk_size * sizeof(int), ptr);
							ptr += res;
							res = pread(fd, e->blkmap_continue, blk_size * sizeof(char), ptr);
							ptr += res;
							break;
					case 1:
							e->dir_entry = NULL;
							if(e->dir_err>0)
								for(i=0;i<e->dir_err;i++){
										dir_entry = malloc(sizeof(struct mfs_hash_dir_entry));
										res = pread(fd, dir_entry, sizeof(struct mfs_hash_dir_entry), ptr);
										ptr +=res;
										dir_entry->next = e->dir_entry;
										e->dir_entry = dir_entry;
								}
							break;
			}
				HASH_ADD_STR(mfs_hash_table, path, e);
#ifdef MFS_DEBUG
				printf("ptr is %d\n", ptr);
#endif


		}
}
int main(int argc, char **argv)
{
	/* FUSE needed variables */
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	int multithreaded = 1;
	int foreground = 1;
	struct stat st;
	
	/* End */
	int res;

	/* constants */
	init_mfs_struct_len();
	init_mfs_constants();
#ifdef MFS_DEBUG
	printf("MFS_MEMORY_CACHE_SIZE%d\n"
			"MFS_DISK_CACHE_SIZE%d\n", MFS_MEMORY_CACHE_SIZE,
			MFS_DISK_CACHE_SIZE);
#endif

	pthread_create(&io_watcher_tid, NULL, mfs_IO_watcher, NULL);

#ifdef MFS_HAVE_CACHE
	pthread_mutex_init(&mem_list_lock, NULL);
	pthread_create(&cache_tid, NULL, mfs_save_mem_cache, NULL);
	pthread_mutex_init(&mfs_LRU_lock, NULL);
	pthread_create(&hd_cache_clean_tid, NULL, clean_mfs_LRU_hd_element, NULL);
#endif
#ifdef MFS_HAVE_DISK_CACHE
	pthread_mutex_init(&mfs_LRU_hd_lock,NULL);
	mfs_LRU_hd_blank=malloc(sizeof(char) * 256);
#endif
#ifdef MFS_HAVE_READHEAD
	pthread_create(&ra_lv1_tid, NULL, mfs_ra_do_current_list, NULL);
	pthread_mutex_init(&ra_current_lock, NULL);
	pthread_create(&ra_tid1, NULL, mfs_do_readhead, NULL);
	pthread_create(&ra_tid2, NULL, mfs_do_readhead, NULL);
	pthread_create(&ra_tid3, NULL, mfs_do_readhead, NULL);
	pthread_create(&ra_tid4, NULL, mfs_do_readhead, NULL);
	pthread_create(&ra_tid5, NULL, mfs_do_readhead, NULL);
	pthread_create(&ra_tid6, NULL, mfs_do_readhead, NULL);
#endif
#ifdef MFS_HAVE_READHEAD_SEQ
	pthread_create(&ra_seq_tid, NULL, mfs_do_readhead_seq, NULL);
#endif
	pthread_create(&upload_tid, NULL, mfs_upload_task, NULL);
	pthread_mutex_init(&mfs_hash_lock, NULL);
/* initialize hash */
	init_mfs_hash_element_pool();

	pthread_mutex_init(&mfs_task_lock, NULL);
	pthread_mutex_lock(&mfs_task_lock);
	mfs_task_list = NULL;
	mfs_net_connect = 0;
	pthread_mutex_unlock(&mfs_task_lock);

	pthread_create(&network_tid, NULL, mfs_network, NULL);

	if(signal(SIGPIPE, mfs_network_disconnect)  == SIG_ERR){
		printf("Can not catch SIGINT\n");
	}
	if(signal(SIGINT, safe_quit_mfs)  == SIG_ERR){
		printf("Can not catch SIGINT\n");
	}
	if(signal(SIGKILL, safe_quit_mfs)  == SIG_ERR){
		printf("Can not catch SIGINT\n");
	}

	/* test for FUSE creation */
	printf("strart\n");

	/* load previous file elements */
	mfs_load_file_element();

	res = fuse_parse_cmdline(&args, &mountpoint, &multithreaded, &foreground);

	if(res == -1)
		exit(1);	

	res = stat(mountpoint, &st);
	if(res == -1){
		printf("%s error\n", mountpoint);
	}

	ch = fuse_mount(mountpoint, &args);
	if(!ch)
		exit(1);

	res = fcntl(fuse_chan_fd(ch), F_SETFD, FD_CLOEXEC);
	if(res == -1)
		printf("FD_CLOESEC on fuse dev error\n");

	fuse = fuse_new(ch, &args, &mfs_oper, sizeof(struct fuse_operations), NULL);
	if(fuse == NULL) {
		fuse_unmount(mountpoint, ch);
		exit(1);
	}


	/* it seem not running until get exited */
	res = fuse_set_signal_handlers(fuse_get_session(fuse));
	if(res == -1){
		fuse_unmount(mountpoint, ch);
		fuse_destroy(fuse);
		exit(1);
	}

	res = fuse_loop_mt(fuse);

	res = res == -1 ? 1 : 0;

	fuse_remove_signal_handlers(fuse_get_session(fuse));
	fuse_unmount(mountpoint, ch);
	fuse_destroy(fuse);
	free(mountpoint);
	fuse_opt_free_args(&args);

	return res;
}

static void mfs_network_disconnect(int tmp)
{
		mfs_net_connect = 0;
}

static void safe_quit_mfs(int tmp)
{
	printf("cought quit signal\n");
	fuse_remove_signal_handlers(fuse_get_session(fuse));
	fuse_unmount(mountpoint, ch);
	fuse_destroy(fuse);
	free(mountpoint);
	/* mfs clean up functions */
	mfs_stop_thread=1;
	printf("waiting for stop service...\n");
	pthread_join(ra_lv1_tid, NULL);
	pthread_join(cache_tid, NULL);
	pthread_join(ra_tid1, NULL);
	pthread_join(ra_tid2, NULL);
	pthread_join(ra_tid3, NULL);
	pthread_join(ra_tid4, NULL);
	pthread_join(ra_tid5, NULL);
	pthread_join(ra_tid6, NULL);
	pthread_join(ra_seq_tid, NULL);
	pthread_join(upload_tid, NULL);
	pthread_join(network_tid, NULL);
	pthread_join(io_watcher_tid, NULL);
	pthread_join(hd_cache_clean_tid, NULL);
	pthread_join(cache_clean_tid, NULL);
	printf("all threads closed\n");
	printf("clean up cache\n");
	mfs_cleanup();
	printf("cleaned up\n");

	exit(0);
}
