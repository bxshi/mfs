OFFSET64 = -D_FILE_OFFSET_BITS=64 -D_REENTRANT
FUSELIB  = /usr/local/lib/libfuse.so /usr/local/lib/libulockmgr.so
FILEDIR  = /home/bxshi/mfs/test
DEBUG  	 = -g
MFS_SHAREDDIR = /home/bxshi/sharedfs
CC = gcc
EXES = mfs_server mfs_client

#CFLAG = $(OFFSET64) $(FUSELIB) $(REDISLIB) $(DEBUG) -lrt -pthread -I/usr/local/lib
CFLAG = -pthread -lrt $(OFFSET64) $(FUSELIB) 
CLIB  = -pthread -lrt $(FUSELIB) 
OPTCFLAG = `pkg-config --cflags mxml` 
OPTCLIB = `pkg-config --libs mxml` 
all: clean debug

clean:
	rm -fr $(FILEDIR)/cache
	mkdir $(FILEDIR)/cache
	rm -fr *.o
	rm -fr ${EXES}
	rm -rf /dev/shm/*mfs_*

init:init_mfs_client_dir

init_mfs_client_dir:
	mkdir -p ${FILEDIR}/cache
init_mfs_server_dir:
	mkdir -p ${MFS_SHAREDDIR}
	
all: debug

debug: mfs_client mfs_server

mfs_client: mfs_client.o mfs_net.o mfs_file.o mfs_hash.o mfs_opt.o sysusage.o
	${CC} -o  $@ ${OFFSET64} mfs_client.o mfs_opt.o mfs_net.o mfs_file.o mfs_hash.o sysusage.o $(CLIB) $(OPTCLIB)
	
mfs_client.o: mfs_client.c 
	${CC} -c -g $(OFFSET64) mfs_client.c $(CLIB)

sysusage.o: sysusage.c sysusage.h
	${CC} -c ${OFFSET64} sysusage.c sysusage.h
mfs_opt.o: mfs_opt.c mfs_opt.h
	${CC} -c $(OFFSET64) mfs_opt.c mfs_opt.h $(OPTCFLAG)

mfs_server: mfs_server.o mfs_net.o mfs_hash.o
	gcc mfs_server.o mfs_net.o mfs_hash.o $(OFFSET64) -o mfs_server -pthread -lrt

mfs_server.o: mfs_server.c
	gcc -c -g $(OFFSET64) mfs_server.c -pthread -lrt

mfs_file.o: mfs_file.c mfs_file.h
	gcc -c -g $(OFFSET64) mfs_file.c

mfs_net.o: mfs_net.c mfs_net.h
	gcc -c -g $(OFFSET64)  mfs_net.c

mfs_hash.o: mfs_hash.c mfs_hash.h uthash.h
	gcc -c -g $(OFFSET64) mfs_hash.c
