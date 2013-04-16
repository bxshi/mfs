/*
 * =====================================================================================
 *
 *       Filename:  mfs_opt.h
 *
 *    Description:  phrase xml config file
 *
 *        Version:  1.0
 *        Created:  04/11/2012 06:15:18 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include <stdlib.h>
#include <mxml.h>

/* global variables */

size_t MFS_BLOCK_SIZE;
size_t MFS_MEMORY_CACHE_SIZE;
size_t MFS_DISK_CACHE_SIZE;
size_t MFS_DISK_CACHE_SPLIT_SIZE;
char   MFS_CACHE_PATH[4096];
char	MFS_CACHE_FILE[4096];
size_t MFS_CACHE_PATH_LEN;
char   MFS_REMOTE_SERVER[4096];
int    MFS_REMOTE_PORT;
unsigned long MFS_IO_WATCHER_EVAL;
int	   MFS_MEMORY_CACHE_ACTIVE_IO_BARRIER;
int	   MFS_RA_ACTIVE_IO_BARRIER;
int    MFS_UPLOAD_ACTIVE_IO_BARRIER;
int	   MFS_MEMORY_CACHE_ACTIVE_CPU_BARRIER;
int	   MFS_RA_ACTIVE_CPU_BARRIER;
int    MFS_UPLOAD_ACTIVE_CPU_BARRIER;
int 	MFS_CLIENT_SOCKET_THREADS;
int		MFS_MEM_CLEAN_SIZE;
int		MFS_MEM_CLEAN_TIME;
int		MFS_HD_CLEAN_SIZE;
int		MFS_HD_CLEAN_TIME;

int load_mfs_config(mxml_node_t *tree);
void init_mfs_constants();
