#ifndef AFFINITY_SCHEDULER_H
#define AFFINITY_SCHEDULER_H

#include <omp.h>

typedef struct 
{
	// id
	int myid;	

	// lock
	omp_lock_t mylock;

	// my assigned work
	int assignedLo;
	int assignedHi;
	
	// my work in hand
	int currentLo;
	int currentHi;

	// remaining
	int remaining;

} LocalWorkQueue;

// public functions
void setMinChunkSize(int minChunkSize);
void initializeLocalWorkQueue(int myid,int nthreads,int lo,int hi,int * startLo,int * startHi,LocalWorkQueue ** my_work_queue_ptr_ptr);
void schedule(int startLo,int startHi,int nthreads,LocalWorkQueue ** global_work_queue,void (*func)(int,int));
     
#endif
