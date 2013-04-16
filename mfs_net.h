/*
 * =====================================================================================
 *
 *       Filename:  mfs_net.h
 *
 *    Description:  command types, structures and functions
 *
 *        Version:  1.0
 *        Created:  12/29/2011 02:30:58 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Baoxu Shi (), bxshi.nku@gmail.com
 *        Company:  Nankai University, Tianjin, China
 *
 * =====================================================================================
 */

#ifndef _MFS_NET_H_
#define _MFS_NET_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

/*
 * Constants of Command Types
 */
#define MFS_GETATTR 0x00000000
#define MFS_READDIR 0x00000001
#define MFS_OPEN    0x00000002
#define MFS_READ    0x00000003
#define MFS_WRITE   0x00000004
#define MFS_WAIT    0x00000005
#define MFS_READHEAD 0x00000006
/* 
 * Constants of Reply Types
 */
#define MFS_REPLY_GETATTR 0x00000000
#define MFS_REPLY_READDIR 0x00000001
#define MFS_REPLY_OPEN 	  0x00000002
#define MFS_REPLY_READ 	  0x00000003
#define MFS_REPLY_WRITE   0x00000004
#define MFS_REPLY_WAIT    0x00000005
#define MFS_REPLY_READHEAD 0x00000006

/* 
 * Get Command/Reply Types
 */

#define MFS_IS_GETATTR(m) (((m) & 0x0000000F ) == 0x00000000)
#define MFS_IS_READDIR(m) (((m) & 0x0000000F ) == 0x00000001)
#define MFS_IS_OPEN(m)    (((m) & 0x0000000F ) == 0x00000002)
#define MFS_IS_READ(m)    (((m) & 0x0000000F ) == 0x00000003)
#define MFS_IS_WRITE(m)   (((m) & 0x0000000F ) == 0x00000004)
#define MFS_IS_WAIT(m)    (((m) & 0x0000000F ) == 0x00000005)
#define MFS_IS_READHEAD(m)    (((m) & 0x0000000F ) == 0x00000006)

/* 
 * Strctures and lengths of Commands
 */

#define mfs_strlen(str) (strlen(str) + 1)

int mfs_net_connect;

int mfs_command_len;
struct mfs_command{
	int type;
	size_t path_len;
};

int static mfs_reply_len;
struct mfs_reply{
	int type;
	int reply;
};

int mfs_pack_info_len;
struct mfs_pack_info{
	size_t pack_len;
};

int mfs_stat_len;
#define mfs_stat stat

int mfs_dir_len;
struct mfs_dir{
	ino_t st_ino;
	mode_t st_mode;
	char d_name[256];
};

int mfs_read_info_len;
struct mfs_read_info{
	size_t offset;
	size_t len;
};

struct mfs_task{
	int connfd;
	void *pack;
	struct mfs_task *next;
};
struct mfs_task *mfs_task_list;
pthread_mutex_t mfs_task_lock;
/* 
 * Functions about commands 
 */
void init_mfs_task(int cnt, char *url, int port);
struct mfs_task* get_mfs_task();
void put_mfs_task(struct mfs_task *task);

 int init_mfs_struct_len();

 int init_mfs_server_socket(int port);
 int init_mfs_client_socket(char *url, int port);
 int init_mfs_server_socket_thread(char *url, int port);

/* reliable send method
 * return value: -1 for error, 0 for ok*/
 int send_mfs(int sockfd, void *buff, size_t len);

/* reliable get method
 * return value: -1 for error, 0 for ok */
 int get_mfs(int sockfd, void *buff, size_t len);

 int send_mfs_command(int sockfd, int type, size_t path_len);

/* get mfs_command
 * size_t path_len : return the path_len
 * return value: -1 for error, or the type of this command */
 int get_mfs_command(int sockfd, size_t *path_len);

 int send_mfs_reply(int sockfd, int type, int reply);

/* get mfs_reply
 * int *reply : return the reply
 * return value: -1 for error, or the type of this command */
 int get_mfs_reply(int sockfd, int *reply);

 int send_mfs_path_name(int sockfd, char *path_name);

/* get path_name
 * return value: -1 for error, 0 for ok */
 int get_mfs_path_name(int sockfd, size_t path_len, char *path_name);

 int send_mfs_pack_info(int sockfd, size_t pack_len);

/* get pack_len
 * return value: -1 for error, or pack_len */
 int get_mfs_pack_info(int sockfd);

// int send_mfs_dir(int sockfd, int st_mode, size_t st_size, char *d_name);

extern int send_mfs_dir(int sockfd, ino_t d_ino, unsigned char d_type, char *d_name);
/* get mfs_dir
 * return values by pointers */
// int get_mfs_dir(int sockfd, size_t pack_len, int *st_mode, size_t *st_size, char *d_name);
extern int get_mfs_dir(int sockfd, size_t pack_len, mode_t *st_mode, ino_t *st_ino, char *d_name);

 int send_mfs_stat(int sockfd, struct mfs_stat *stbuf);

/* get stat
 * struct mfs_stat *stbuf : return stat
 * return value: -1 for error, 0 for ok */
 int get_mfs_stat(int sockfd, struct mfs_stat *stbuf);

 int send_mfs_read_info(int sockfd, size_t offset, size_t len);

/* get mfs_read_info
 * return values by pointers */
 int get_mfs_read_info(int sockfd, size_t *offset, size_t *len);

 int send_mfs_read_buff(int sockfd, char *read_buff, size_t len);

/* get buff
 * return buff by pointer */
 int get_mfs_read_buff(int sockfd, char *read_buff, size_t len);

#endif
