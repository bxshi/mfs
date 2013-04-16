/*
 * =====================================================================================
 *
 *       Filename:  mfs_net.c
 *
 *    Description:  implementations of socket based transfer functions
 *
 *        Version:  1.0
 *        Created:  12/29/2011 04:35:06 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Baoxu Shi (), bxshi.nku@gmail.com
 *        Company:  Nankai University, Tianjin, China
 *
 * =====================================================================================
 */

#include "mfs_net.h"

//#define MFS_DEBUG

void init_mfs_task(int cnt, char *url, int port)
{
	struct mfs_task *task;
	int i;


	if (mfs_net_connect == 0){
		while(mfs_task_list != NULL){//close unuseable fds
			close(mfs_task_list->connfd);
			task = mfs_task_list;
			mfs_task_list = task->next;
			free(task);
		}
		for(i=0;i<cnt;i++){
			task = malloc(sizeof(struct mfs_task));
			task->pack = NULL;
			task->connfd = init_mfs_client_socket(url, port);
			if(task->connfd != -1){
				task->next = mfs_task_list;
				mfs_task_list = task;
				mfs_net_connect = 1;
			}
			else{
					free(task);
					mfs_net_connect = 0;
					break;
			}
		}
	}
}

struct mfs_task* get_mfs_task(){
	struct mfs_task *task;
	
	pthread_mutex_lock(&mfs_task_lock);
	task = mfs_task_list;
	while(task == NULL){
		pthread_mutex_unlock(&mfs_task_lock);
		return NULL;
	}
	mfs_task_list = mfs_task_list->next;
	pthread_mutex_unlock(&mfs_task_lock);

	return task;
}
void put_mfs_task(struct mfs_task *task){

	task->pack = NULL;
	task->next = NULL;
	pthread_mutex_lock(&mfs_task_lock);
	task->next = mfs_task_list;
	mfs_task_list = task;
	pthread_mutex_unlock(&mfs_task_lock);

}

extern int init_mfs_struct_len(){


	mfs_command_len   = sizeof(struct mfs_command);
	mfs_reply_len  	  = sizeof(struct mfs_reply);
	mfs_pack_info_len = sizeof(struct mfs_pack_info);
	mfs_stat_len      = sizeof(struct mfs_stat);
	mfs_read_info_len = sizeof(struct mfs_read_info);
	mfs_dir_len       = sizeof(struct mfs_dir);

	return 0;
}

extern int init_mfs_server_socket(int port){
	
	int sockfd;
	socklen_t clien;
	struct sockaddr_in servaddr;

	
	/* initial sync server, waiting for connection*/
	bzero(&servaddr, sizeof(servaddr));
	
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		printf("can not create socket\n");
		return -1;
	}
		
	if ((bind(sockfd, (struct sockaddr*) &servaddr, sizeof(servaddr))) == -1){
		printf("can not bind port\n");
		return -1;
	}
	if ((listen(sockfd, 1024)) == -1){
		printf("can not listen to the bind port\n");
		return -1;
	}

	return sockfd;
}

extern int init_mfs_client_socket(char *url, int port){

	int sockfd;
	struct sockaddr_in servaddr;
	ssize_t n;

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		perror("can not create TCP socket!\n");
	
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);

	if((inet_pton(AF_INET, url, &servaddr.sin_addr)) <= 0) 
		perror("not a legal address\n");

	if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		perror("can not establish connection\n");

	return sockfd;
}

extern int send_mfs(int sockfd, void *buff, size_t len){
	
	int res;
	int tmp;
	
	res = 0;

	while(res != len){
		tmp = send(sockfd, (char *)buff + res, len - res, 0);
		if(tmp == -1){
#ifdef MFS_DEBUG
			printf("<1>: send_mfs error, error is :%s \n", (char *)strerror(errno));
			printf("<1>: need send %d bytes, already sent %d bytes, still left %d bytes\n",
				len, res, len-res);
#endif
			return -1;
		}
		res +=tmp;
	}
#ifdef MFS_DEBUG
	printf("<1>: need send %d bytes, sent %d bytes\n", len, res);
#endif
	return 0;
}

extern int get_mfs(int sockfd, void *buff, size_t len){
	
	int res;
	int tmp;

	res = 0;

	while(res != len){
		tmp = recv(sockfd, (char *)buff + res, len - res, 0);
		if(tmp == -1){
#ifdef MFS_DEBUG
			printf("<1>: %s error, error is :%s \n", __func__, (char *)strerror(errno));
			printf("<1>: need get %d bytes, already got %d bytes, still left %d bytes\n",
				len, res, len-res);
#endif
			return -1;
		}
		res +=tmp;
	}
#ifdef MFS_DEBUG
	printf("<1>: need get %d bytes, got %d bytes\n", len, res);
#endif
	return 0;
}

extern int send_mfs_command(int sockfd, int type, size_t path_len){

	int res;
	struct mfs_command mfscmd;
	
	mfscmd.type = type;
	mfscmd.path_len = path_len;
	
	if(send_mfs(sockfd, &mfscmd, mfs_command_len) == 0){
			return 0;
	}
	
#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int get_mfs_command(int sockfd, size_t *path_len){
	
	int res;
	struct mfs_command mfscmd;

	if(get_mfs(sockfd, &mfscmd, mfs_command_len) == 0){
		*path_len = mfscmd.path_len;
		return mfscmd.type;
	}

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int send_mfs_reply(int sockfd, int type, int reply){
	
	int res;
	struct mfs_reply mfsreply;

	mfsreply.type = type;
	mfsreply.reply = reply;

	if(send_mfs(sockfd, &mfsreply, mfs_reply_len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int get_mfs_reply(int sockfd, int *reply){

	struct mfs_reply mfsreply;

	if(get_mfs(sockfd, &mfsreply, mfs_reply_len) == 0){
		*reply = mfsreply.reply;
		return mfsreply.type;
	}

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int send_mfs_path_name(int sockfd, char *path_name){

	if(send_mfs(sockfd, path_name, mfs_strlen(path_name)) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int get_mfs_path_name(int sockfd, size_t path_len, char *path_name){

	if(get_mfs(sockfd, path_name, path_len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int send_mfs_pack_info(int sockfd, size_t pack_len){

	struct mfs_pack_info mfspackinfo;
	
	mfspackinfo.pack_len = pack_len;

	if(send_mfs(sockfd, &mfspackinfo, mfs_pack_info_len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int get_mfs_pack_info(int sockfd){
	
	struct mfs_pack_info mfspackinfo;

	if(get_mfs(sockfd, &mfspackinfo, mfs_pack_info_len) == 0)
		return mfspackinfo.pack_len;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int send_mfs_dir(int sockfd, ino_t d_ino, unsigned char d_type, char *d_name){
	
	size_t len;
	struct mfs_dir *mfsdir;

	len = mfs_dir_len;

	mfsdir = malloc(len);

	mfsdir->st_ino = d_ino;
	mfsdir->st_mode = d_type<<12;
	strcpy(mfsdir->d_name, d_name);

	if(send_mfs(sockfd, mfsdir, len) == 0){
		return 0;
		free(mfsdir);
	}

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	free(mfsdir);
	return -1;
}

extern int get_mfs_dir(int sockfd, size_t pack_len, mode_t *st_mode, ino_t *st_ino, char *d_name)
{
	struct mfs_dir *mfsdir;
	
	mfsdir = malloc(pack_len);	
	if(get_mfs(sockfd, mfsdir, pack_len) == 0){
		*st_mode = mfsdir->st_mode;
		*st_ino = mfsdir->st_ino;
		strcpy(d_name, mfsdir->d_name);
		free(mfsdir);
		return 0;
	}
#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	free(mfsdir);
	return -1;
}

extern int send_mfs_stat(int sockfd, struct mfs_stat *stbuf){
	
	if(send_mfs(sockfd, stbuf, mfs_stat_len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int get_mfs_stat(int sockfd, struct mfs_stat *stbuf){

	if(get_mfs(sockfd, stbuf, mfs_stat_len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int send_mfs_read_info(int sockfd, size_t offset, size_t len){

	struct mfs_read_info mfsreadinfo;

	mfsreadinfo.offset = offset;
	mfsreadinfo.len    = len;

	if(send_mfs(sockfd, &mfsreadinfo, mfs_read_info_len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int get_mfs_read_info(int sockfd, size_t *offset, size_t *len){

	struct mfs_read_info mfsreadinfo;

	if(get_mfs(sockfd, &mfsreadinfo, mfs_read_info_len) == 0){
		*offset = mfsreadinfo.offset;
		*len    = mfsreadinfo.len;
		return 0;
	}

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int send_mfs_read_buff(int sockfd, char *read_buff, size_t len){

	if(send_mfs(sockfd, read_buff, len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}

extern int get_mfs_read_buff(int sockfd, char *read_buff, size_t len){
	
	if(get_mfs(sockfd, read_buff, len) == 0)
		return 0;

#ifdef MFS_DEBUG
	printf("<2>%s error\n", __func__);
#endif
	return -1;
}
