/*
 * =====================================================================================
 *
 *       Filename:  mfs_opt.c
 *
 *    Description:  phrase xml config file
 *
 *        Version:  1.0
 *        Created:  04/11/2012 05:13:15 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include "mfs_opt.h"

int load_mfs_config(mxml_node_t *tree)
{
	mxml_node_t *node;

	node = mxmlFindElement(tree, tree, "mfs_block_size",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_BLOCK_SIZE = 4096;
	}else{
			MFS_BLOCK_SIZE = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "max_memory_cache_size",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_MEMORY_CACHE_SIZE = 1024;
	}else{
			MFS_MEMORY_CACHE_SIZE = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "max_disk_cache_size",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_DISK_CACHE_SIZE = 1024;
	}else{
			MFS_DISK_CACHE_SIZE = atoi(mxmlGetText(node,0));
	}
			MFS_DISK_CACHE_SIZE *= 1024*1024;

	node = mxmlFindElement(tree, tree, "disk_cache_split_size",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_DISK_CACHE_SPLIT_SIZE = 1024;
	}else{
			MFS_DISK_CACHE_SPLIT_SIZE = atoi(mxmlGetText(node,0)) * 1024;
	}
	MFS_DISK_CACHE_SPLIT_SIZE = MFS_DISK_CACHE_SPLIT_SIZE / 4;

	node = mxmlFindElement(tree, tree, "cache_path",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			strcpy(MFS_CACHE_PATH, "/home/bxshi/mfs/test/cache/");
	}else{
			strcpy(MFS_CACHE_PATH, mxmlGetText(node,0));
	}
	MFS_CACHE_PATH_LEN = strlen(MFS_CACHE_PATH);

	node = mxmlFindElement(tree, tree, "cache_file",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			strcpy(MFS_CACHE_FILE, "/home/bxshi/mfs/test/cache/");
	}else{
			strcpy(MFS_CACHE_FILE, mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "remote_server",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			strcpy(MFS_REMOTE_SERVER, "127.0.0.1");
	}else{
			strcpy(MFS_REMOTE_SERVER, mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "remote_port",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_REMOTE_PORT = 11112;
	}else{
			MFS_REMOTE_PORT = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "io_watcher_eval",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_IO_WATCHER_EVAL = 500000;
	}else{
			MFS_IO_WATCHER_EVAL = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "memory_cache_active_io_barrier",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_MEMORY_CACHE_ACTIVE_IO_BARRIER = 85;
	}else{
			MFS_MEMORY_CACHE_ACTIVE_IO_BARRIER = atoi(mxmlGetText(node,0));
	}
	node = mxmlFindElement(tree, tree, "memory_cache_active_cpu_barrier",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_MEMORY_CACHE_ACTIVE_CPU_BARRIER = 85;
	}else{
			MFS_MEMORY_CACHE_ACTIVE_CPU_BARRIER = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "readahead_active_io_barrier",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_RA_ACTIVE_IO_BARRIER = 50;
	}else{
			MFS_RA_ACTIVE_IO_BARRIER = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "readahead_active_cpu_barrier",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_RA_ACTIVE_CPU_BARRIER = 50;
	}else{
			MFS_RA_ACTIVE_CPU_BARRIER = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "upload_active_io_barrier",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_UPLOAD_ACTIVE_IO_BARRIER = 10;
	}else{
			MFS_UPLOAD_ACTIVE_IO_BARRIER = atoi(mxmlGetText(node,0));
	}
	node = mxmlFindElement(tree, tree, "upload_active_cpu_barrier",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_UPLOAD_ACTIVE_CPU_BARRIER = 10;
	}else{
			MFS_UPLOAD_ACTIVE_CPU_BARRIER = atoi(mxmlGetText(node,0));
	}

	node = mxmlFindElement(tree, tree, "client_socket_threads",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_CLIENT_SOCKET_THREADS = 10;
	}else{
			MFS_CLIENT_SOCKET_THREADS = atoi(mxmlGetText(node, 0));
	}

	node = mxmlFindElement(tree, tree, "memory_clean_precent",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_MEM_CLEAN_SIZE = 80;
	}else{
			MFS_MEM_CLEAN_SIZE = atoi(mxmlGetText(node, 0));
	}
	MFS_MEM_CLEAN_SIZE = (float)MFS_MEM_CLEAN_SIZE / 100 * (float)MFS_MEMORY_CACHE_SIZE;

	node = mxmlFindElement(tree, tree, "disk_clean_precent",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_HD_CLEAN_SIZE = 80;
	}else{
			MFS_HD_CLEAN_SIZE = atoi(mxmlGetText(node, 0));
	}
	MFS_HD_CLEAN_SIZE = ((float)MFS_HD_CLEAN_SIZE / 100) * (float)MFS_DISK_CACHE_SIZE;

	node = mxmlFindElement(tree, tree, "disk_clean_timeval",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_HD_CLEAN_TIME = 30;
	}else{
			MFS_HD_CLEAN_TIME = atoi(mxmlGetText(node, 0));
	}

	node = mxmlFindElement(tree, tree, "memory_clean_timeval",
							NULL, NULL, MXML_DESCEND);
	if(node == NULL){
			MFS_MEM_CLEAN_TIME= 30;
	}else{
			MFS_MEM_CLEAN_TIME = atoi(mxmlGetText(node, 0));
	}

	return 0;
}

void init_mfs_constants()
{
	FILE *fp;
	mxml_node_t *tree;

	fp = fopen("mfs.conf", "r");
	tree = mxmlLoadFile(NULL, fp, MXML_TEXT_CALLBACK);
	fclose(fp);

	load_mfs_config(tree);

	mxmlDelete(tree);
}
#ifdef MFS_OPT_DEBUG
int main(int argc, char **argv)
{
	FILE *fp;
	mxml_node_t *tree;

	fp = fopen("mfs.conf", "r");
	tree = mxmlLoadFile(NULL, fp, MXML_TEXT_CALLBACK);
	fclose(fp);

	load_mfs_config(tree);

	printf("\t MFS_BLOCK_SIZE %d \t MFS_MEMORY_CACHE_SIZE %d \t\n"
			"\t MFS_DISK_CACHE_SIZE %d \t MFS_DISK_CACHE_SPLIT_SIZE%d \t\n"
			"\t MFS_CACHE_PATH %s \n"
			"\t MFS_CACHE_PATH_LEN %d \t\n", MFS_BLOCK_SIZE, MFS_MEMORY_CACHE_SIZE,
			MFS_DISK_CACHE_SIZE, MFS_DISK_CACHE_SPLIT_SIZE, MFS_CACHE_PATH, 
			MFS_CACHE_PATH_LEN);

	mxmlDelete(tree);
	return 0;
}
#endif
