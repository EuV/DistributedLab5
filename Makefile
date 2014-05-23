pa5: main.c la5.h ipc.c clean
	gcc -std=c99 -Wall -pedantic *.c -o pa5 -L. -lruntime

tar: main.c la5.h ipc.c clean
	mkdir pa5
	cp main.c la5.h ipc.c pa5
	tar -czvf pa5.tar.gz pa5
	rm -rf pa5


clean:
	-rm -rf *.o *.log pa5 pa5.tar.gz

lib:
	export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:~/pa5/DistributedLab5" LD_PRELOAD=~/pa5/DistributedLab5/libruntime.so
