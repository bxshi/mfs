/*
 * =====================================================================================
 *
 *       Filename:  mfs_server.c
 *
 *    Description:  Mirrorfs Remote Server
 *
 *        Version:  1.0
 *        Created:  12/29/2011 05:49:04 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Baoxu Shi (), bxshi.nku@gmail.com
 *        Company:  Nankai University, Tianjin, China
 *
 * =====================================================================================
 */
#define MFS_DEBUG
#include "mfs_net.h"
#include "mfs_hash.h"
#include <signal.h>
#include <pthread.h>
#include <time.h>


pthread_t tid[32];
pthread_t read_tid;
char *pathfix;
size_t pathfix_len;
int connfd[32];

static void safe_quit_mfs_serv(int);

int mfs_serv_attr(int connfd, size_t path_len);
int mfs_serv_dir(int connfd, size_t path_len);
int mfs_serv_read(int connfd, size_t path_len);
int mfs_serv_write(int connfd, size_t path_len);
void *mfs_serv_thread(void *argc);
int sockfd;

int main(int argc, char **argv){


	int i;
	int res;
	int *pconnfd;
	socklen_t clien;
	struct sockaddr_in cliaddr;

	if(argc !=3){
		printf("Usage: mfs_server <port> <local directory>\n");
		return -1;
	}

	/* change default operations about signals*/
	if(signal(SIGINT, safe_quit_mfs_serv)  == SIG_ERR){
		printf("Can not catch SIGINT\n");
	}
	if(signal(SIGABRT, safe_quit_mfs_serv) == SIG_ERR){
		printf("Can not catch SIGABRT\n");
	}
	if(signal(SIGTERM, safe_quit_mfs_serv) == SIG_ERR){
		printf("Can not catch SIGTERM\n");
	}

	pathfix = malloc(mfs_strlen(argv[2]));
	strcpy(pathfix, argv[2]);
	
	/* use for offset, so do not count the final \0 */
	pathfix_len = strlen(pathfix);

	sockfd = init_mfs_server_socket(atoi(argv[1]));
	if(sockfd == -1)
		return -1;
	printf("Server socket initialized\n");

	init_mfs_struct_len();
	printf("Server constants initialized\n");

	bzero(&cliaddr, sizeof(cliaddr));
	clien =sizeof(cliaddr);
	
	i=0;

	while(i<32) {
		connfd[i] = accept(sockfd, (struct sockaddr *)&cliaddr, &clien);
		printf("Connection established, client %s:%d\n", 
			inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
		res = pthread_create(&tid[i++], NULL, mfs_serv_thread, &connfd[i]);
	}

	while(1){sleep(10000);}
	return 0;

}
void *mfs_serv_thread(void *argc)
{
	int connfd;
	int type;
	size_t path_len;
	connfd = *(int *)argc;
	for(; ;){
		
		type = get_mfs_command(connfd, &path_len);
		
		if(MFS_IS_GETATTR(type)){

			mfs_serv_attr(connfd, path_len);

		}else if(MFS_IS_READDIR(type)){
			
			mfs_serv_dir(connfd, path_len);

		}else if(MFS_IS_OPEN(type)){

		}else if(MFS_IS_READ(type)){

			mfs_serv_read(connfd, path_len);

		}else if(MFS_IS_WRITE(type)){
			mfs_serv_write(connfd, path_len);
		}

		usleep(300);

	}
}
int mfs_serv_attr(int connfd, size_t path_len)
{
	int res;
	char path[1024];
	struct mfs_stat stbuf;

/* mfs_path_name */

 	strcpy(path, pathfix);
	res = get_mfs_path_name(connfd, path_len, (char *)path + pathfix_len);
	
	if(res == -1){
		send_mfs_reply(connfd, MFS_REPLY_GETATTR, EBADF);
		return 0;
	}
	
	res = lstat(path, &stbuf);

	if(res == -1)
		res = errno;
/* mfs_reply */
	send_mfs_reply(connfd, MFS_REPLY_GETATTR, res);
	
	if(res == -1)
		res = errno;
	if(res == 0)	
	send_mfs_stat(connfd, &stbuf);
	
#ifdef MFS_DEBUG
	printf("<3>%s %s %d\n", __func__, path, res);
#endif
	return 0;
}

int mfs_serv_dir(int connfd, size_t path_len)
{
	int res;
	char path[1024];
	struct dirent *de;
	DIR *dp;

 	strcpy(path, pathfix);
	res = get_mfs_path_name(connfd, path_len, (char *)path + pathfix_len);
	*(char *)(path + pathfix_len+path_len-1) = '/';
	*(char *)(path + pathfix_len+path_len) = '\0';
#ifdef MFS_DEBUG
	printf("!!!!!<3>: %s directory: %s \n", __func__, path);
#endif	
	if(res == -1){
		send_mfs_reply(connfd, MFS_REPLY_GETATTR, EBADF);
#ifdef MFS_DEBUG
		printf("<3>%s error\n",__func__);
#endif
		return 0;
	}

	dp = opendir(path);
	if(dp == NULL){
		res = errno;
		send_mfs_reply(connfd, MFS_REPLY_READDIR, res);
#ifdef MFS_DEBUG
		printf("<3>%s opendir error, code : %s\n",__func__, (char *)strerror(res));
#endif
		return 0;
	}

	send_mfs_reply(connfd, MFS_REPLY_READDIR, 0);

	while((de = readdir(dp)) != NULL){
#ifdef MFS_DEBUG
		printf("!!!!!<3>: %s readdir %s \n", __func__, path);
#endif
		send_mfs_pack_info(connfd, mfs_dir_len);
		send_mfs_dir(connfd, de->d_ino, de->d_type, de->d_name);
#ifdef MFS_DEBUG
		printf("<3>%s %s \n", __func__, path);
#endif
	}
	
	send_mfs_pack_info(connfd, 0);
#ifdef MFS_DEBUG
	printf("<3>%s send end\n", __func__);
#endif
	close(dp);
	return 0;
}

int mfs_serv_read(int connfd, size_t path_len)
{
	int res, fd;
	char path[1024];
	size_t len;
	size_t offset;
	char *buf;

 	strcpy(path, pathfix);
	res = get_mfs_path_name(connfd, path_len, path + pathfix_len);
	
	if (res == -1){
		send_mfs_reply(connfd, MFS_REPLY_READ, EBADF);
#ifdef MFS_DEBUG
		printf("<3>%s get_mfs_path_name error\n",__func__);
#endif
		return 0;	
	}

	res = get_mfs_read_info(connfd, &offset, &len);
#ifdef MFS_DEBUG
	printf("<3>: %s read_info offset %d len %d path %s\n", __func__, offset, len, path);
#endif
	if(res == -1){
		send_mfs_reply(connfd, MFS_REPLY_READ, EBADF);
#ifdef MFS_DEBUG
		printf("<3>%s get_mfs_read_info error\n",__func__);
#endif
		return 0;
	}

	fd = open(path, O_RDONLY);
	buf = malloc(len);
	res = pread(fd, buf, len, offset);
	if(res== -1)
		send_mfs_reply(connfd, MFS_REPLY_READ, -errno);
	else{
		send_mfs_reply(connfd, MFS_REPLY_READ, 0);
		close(fd);
		send_mfs_pack_info(connfd, res);
	
		send_mfs_read_buff(connfd, buf, res);
	}
#ifdef MFS_DEBUG
	printf("send %d bytes\n", res);	
#endif

	free(buf);
	return 0;
}

int mfs_serv_write(int connfd, size_t path_len)
{
	int res, fd;
	char path[1024];
	size_t offset, len;
	char *buf;

 	strcpy(path, pathfix);
	res = get_mfs_path_name(connfd, path_len, path + pathfix_len);
	
	if (res == -1){
		send_mfs_reply(connfd, MFS_REPLY_READ, EBADF);
#ifdef MFS_DEBUG
		printf("<3>%s get_mfs_path_name error\n",__func__);
#endif
		return 0;	
	}
	res = get_mfs_read_info(connfd, &offset, &len);
#ifdef MFS_DEBUG
	printf("<3>: %s write_info offset %d len %d\n", __func__, offset, len);
#endif
	if(res == -1){
		send_mfs_reply(connfd, MFS_REPLY_READ, EBADF);
#ifdef MFS_DEBUG
		printf("<3>%s get_mfs_read_info error\n",__func__);
#endif
		return 0;
	}

	buf = malloc(len);

	res = get_mfs_read_buff(connfd, buf, len);
	if(res == -1){
#ifdef MFS_DEBUG
			printf("<3>%s get pack error\n", __func__);
#endif
			free(buf);
			return 0;
	}

	/* need to add stbuf and other informations */
	fd = open(path, O_CREAT|O_WRONLY, 0666);
	if(fd != -1){
		res = pwrite(fd, buf, len, offset);
		close(fd);
#ifdef MFS_DEBUG
	printf("write %d bytes\n", res);	
#endif
		if(res == len)
			send_mfs_reply(connfd, MFS_REPLY_WRITE, 0);
		else
			send_mfs_reply(connfd, MFS_REPLY_WRITE, errno);
	}
}

static void safe_quit_mfs_serv(int tmp)
{
	int i;
	printf("<0>: Got Signal, quitting...\n");
	for(i=0;i<10;i++)
		close(connfd[i]);

	close(sockfd);
	exit(0);
}
