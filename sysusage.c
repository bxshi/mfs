/*
 * =====================================================================================
 *
 *       Filename:  sysusage.c
 *
 *    Description:  get usage of CPU and I/O
 *
 *        Version:  1.0
 *        Created:  04/09/2012 02:23:08 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include "sysusage.h"

struct cpu_stat get_cpu_stat()
{
		FILE *file;
		char cpu[4];
		struct cpu_stat stat;
		file = fopen("/proc/stat", "r");
		
		fscanf(file, "%4s %ld %ld %ld %ld %ld %ld %ld", cpu,
						&stat.user, &stat.nice, &stat.sys, &stat.idle,
						&stat.iowait, &stat.irq, &stat.softirq);
		fclose(file);
		return stat;
}

struct disk_stat get_disk_stat()
{
		FILE *file;
		struct disk_stat stat;

		file = fopen("/sys/block/sda/stat", "r");

		fscanf(file, "\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld", &stat.read_completed,
						&stat.read_merged, &stat.sector_read, &stat.ms_spent_in_read,
						&stat.write_completed, &stat.garabage, &stat.sector_write, &stat.ms_spent_in_write,
						&stat.IO_in_process, &stat.ms_spent_in_IO, &stat.weighted_mfs_spent_in_IO);
		fclose(file);
		return stat;
}

void print_cpu_stat(struct cpu_stat stat)
{
		printf("user:%ld nice:%ld sys:%ld idle:%ld iowait:%ld irq:%ld softirq:%ld\n", stat.user, stat.nice, stat.sys, stat.idle, stat.iowait, stat.irq, stat.softirq);
}
float get_disk_usage(struct disk_stat pre, struct disk_stat after, useconds_t usec)
{
	return (float)((float)(after.ms_spent_in_IO - pre.ms_spent_in_IO) / (float)(usec/1000)) * 100;
}
void print_disk_usage(struct disk_stat pre, struct disk_stat after , useconds_t usec)
{
		float IO_time;

		IO_time = (float)((float)(after.ms_spent_in_IO - pre.ms_spent_in_IO) / (float)(usec/1000)) * 100;
		printf("I/O usage is %.10f Done %ld Reads and %ld Writes\n", IO_time,
						after.read_completed - pre.read_completed,
						after.write_completed - pre.write_completed);

}

float get_cpu_usage(struct cpu_stat pre, struct cpu_stat after)
{
		float total;
		float idle;
		total = after.user+after.nice+after.sys+after.idle+after.iowait+after.irq+after.softirq - 
				(pre.user + pre.nice + pre.sys + pre.idle + pre.iowait + pre.irq + pre.softirq);
		idle = after.idle - pre.idle + after.iowait - pre.iowait;
		return (total-idle) / total * 100;
}
void print_cpu_usage(struct cpu_stat pre, struct cpu_stat after)
{
		float total;
		float idle;
		total = after.user+after.nice+after.sys+after.idle+after.iowait+after.irq+after.softirq - 
				(pre.user + pre.nice + pre.sys + pre.idle + pre.iowait + pre.irq + pre.softirq);
		idle = after.idle - pre.idle + after.iowait - pre.iowait;
		printf("CPU usage is %.2f\n", (total-idle) / total * 100);
}

#ifdef SYSUSAGE_DEBUG
int main(int argc, char ** argv)
{
		struct cpu_stat pre_cpu, after_cpu;
		struct disk_stat pre_disk, after_disk;
		if(argc < 2)
				exit(-1);

		pre_cpu = get_cpu_stat();
		pre_disk = get_disk_stat();

		while(1){
				usleep(atoi(argv[1]));
				after_cpu = get_cpu_stat();
				after_disk = get_disk_stat();

				print_cpu_usage(pre_cpu, after_cpu);
				print_disk_usage(pre_disk, after_disk, atoi(argv[1]));
				pre_cpu = after_cpu;
				pre_disk = after_disk;
		}

		return 0;

}
#endif
