/*
 * =====================================================================================
 *
 *       Filename:  mfs_file.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  12/30/2011 08:48:34 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Baoxu Shi (), bxshi.nku@gmail.com
 *        Company:  Nankai University, Tianjin, China
 *
 * =====================================================================================
 */
 
 #include "mfs_file.h"

int mfs_get_block_map_offset(struct getblkoffset *gboffset, size_t size, size_t offset)
{
	/* Get specific block map section */
	/* Assume offset and size are 4096*n bytes */
	
	gboffset->bmindex = offset / MFS_BLOCKSIZE;
	gboffset->bmoffset = gboffset->bmindex % MFS_BYTESIZE;
	gboffset->bmindex = gboffset->bmindex / MFS_BYTESIZE;

	gboffset->bmsizeindex = size / MFS_BLOCKSIZE;
	gboffset->bmsizeoffset = size % MFS_BLOCKSIZE;
	if(gboffset->bmsizeoffset != 0)
		gboffset->bmsizeoffset = 1;/* Fix if we only write less than 4096 bytes */
	else
		gboffset->bmsizeoffset = gboffset->bmsizeindex % MFS_BYTESIZE;
	gboffset->bmsizeindex = gboffset->bmsizeindex / MFS_BYTESIZE;
	gboffset->bmsizeindex = gboffset->bmsizeoffset == 0? gboffset->bmsizeindex:gboffset->bmsizeindex+1;
	if(gboffset->bmoffset!=0)
		gboffset->bmsizeindex++;
	
	return 0;
}
