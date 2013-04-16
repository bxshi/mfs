/*
 * =====================================================================================
 *
 *       Filename:  mfs_file.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  12/28/2011 08:59:58 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Baoxu Shi (), bxshi.nku@gmail.com
 *        Company:  Nankai University, Tianjin, China
 *
 * =====================================================================================
 */

#ifndef _MFS_FILE_H_
#define _MFS_FILE_H_

#include <fuse.h>
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

#define MFS_BLOCKSIZE 4096 /* 1 bit represent for 4byte */
#define MFS_BYTESIZE  8   

static unsigned char bmmask[11]={0xFF, 0xF0, 0x0F, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
static unsigned char loffmask[9]={0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE};
static unsigned char roffmask[8]={0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01};
static unsigned char setmask[8]={0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

struct getblkoffset{
	size_t bmindex;
	size_t bmoffset;
	size_t bmsizeindex;
	size_t bmsizeoffset;
};

static char zbyte = 'A';

struct mfs_local_getattr{
	int type;
	size_t len;
	int ret;
	struct stat stbuf;
	char path[];
};

struct mfs_local_readdir{
	int type;
	size_t len;
	int ret;
	char path[];
};

struct mfs_local_read{
	int type;
	size_t offset;
	size_t size;
	size_t ret;
	size_t len;
	char path[1024];
	char *data[131072];
};

int mfs_get_block_map_offset(struct getblkoffset *gboffset, size_t size, size_t offset);

#endif
