/*
 * =====================================================================================
 *
 *       Filename:  sysusage.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  04/12/2012 01:33:28 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

struct cpu_stat{
		unsigned long user;
		unsigned long nice;
		unsigned long sys;
		unsigned long idle;
		unsigned long iowait;
		unsigned long irq;
		unsigned long softirq;
};

struct disk_stat{
		unsigned long read_completed;
		unsigned long read_merged;
		unsigned long sector_read;
		unsigned long ms_spent_in_read;
		unsigned long write_completed;
		unsigned long garabage;
		unsigned long sector_write;
		unsigned long ms_spent_in_write;
		unsigned long IO_in_process;
		unsigned long ms_spent_in_IO;
		unsigned long weighted_mfs_spent_in_IO;
};

struct cpu_stat get_cpu_stat();
struct disk_stat get_disk_stat();
void print_cpu_stat(struct cpu_stat stat);
void print_disk_usage(struct disk_stat pre, struct disk_stat after , useconds_t usec);
float get_disk_usage(struct disk_stat pre, struct disk_stat after , useconds_t usec);
float get_cpu_usage(struct cpu_stat pre, struct cpu_stat after);
void print_cpu_usage(struct cpu_stat pre, struct cpu_stat after);

