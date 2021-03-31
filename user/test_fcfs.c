#include "kernel/types.h"
#include "user.h"

void print_performance(struct perf* performance) {
	printf("perf: {\nctime:%d\nttime:%d\nstime:%d\nretime:%d\nruntime:%d\bursttime:%d}\n",
	performance->ctime,performance->ttime,performance->stime,performance->retime,performance->rutime,performance->bursttime);
}

int main(void)
{
	int pid;
	int k = 0;
	printf("test fcfs began\n");
	if ((pid = fork()) > 0) {
		int wstatus;
		struct perf perf;
		wait_stat(&wstatus, &perf);
		printf("Finished waiting for child with pid: %d\n", pid);
		print_performance(&perf);
	} else {
        int pid1;
		if((pid1 = fork()) > 0) {
			for(int i = 0; i < 100000000; i++) {
				k++;
			}
			printf("Grandchild pid is %d, and k is %d\n", pid1, k);
		}
		else {
			for(int i = 0; i < 1000000000; i++) {
				k++;
			}
			printf("i'm the grandchild with k:%d\n",k);
		}
	}
    exit(0);
}