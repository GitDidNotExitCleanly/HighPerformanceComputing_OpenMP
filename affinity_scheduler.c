#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h>

#include "affinity_scheduler.h"

// thread private
static LocalWorkQueue local_work_queue;
#pragma omp threadprivate(local_work_queue)

// shared by all threads
static int MIN_CHUNK_SIZE = 1;

static void updateLocalWorkQueue(int nthreads,int * currentLo,int * currentHi) {

	/*
	 * update local work queue
	 */

	// lock mylock
	omp_lock_t * my_lock_ptr = &(local_work_queue.mylock);
	omp_set_lock(my_lock_ptr);

	int lastHi = local_work_queue.currentHi;
	int assignedHi = local_work_queue.assignedHi;

	if (lastHi == assignedHi) {
		local_work_queue.currentLo = lastHi;
	}
	else {
		local_work_queue.currentLo = lastHi;

		// calculate next chunk size
		int temp_chunkSize = (int)((double)(assignedHi-lastHi) * ((double)1/(double)nthreads));
		int chunkSize = temp_chunkSize <= MIN_CHUNK_SIZE ? MIN_CHUNK_SIZE : temp_chunkSize;

		local_work_queue.currentHi = (lastHi+chunkSize) > assignedHi? assignedHi : (lastHi+chunkSize);

		local_work_queue.remaining = assignedHi-local_work_queue.currentHi;
	}
	
	// update my work in hand
	*currentLo = local_work_queue.currentLo;
	*currentHi = local_work_queue.currentHi;

	// unlock mylock
	omp_unset_lock(my_lock_ptr);
}

static void stealWork(int nthreads,LocalWorkQueue ** global_work_queue,int * lo,int * hi) {

	/*
	 * steal work from victims
	 */

	// Find Target

	LocalWorkQueue * dest_work_queue_ptr = NULL;
	int maxRemaining = -1;	

	omp_lock_t * last_lock_ptr = NULL;
	omp_lock_t * current_lock_ptr = NULL;

	for (int i=0;i<nthreads;i++) {
		LocalWorkQueue * current_work_queue_ptr = global_work_queue[i];		

		int current_remaining = current_work_queue_ptr->remaining;
		current_lock_ptr = &(current_work_queue_ptr->mylock);

		// evaluate current victim
		int isWorthy = 0;
		if (current_remaining > 0) {
			if (current_remaining > MIN_CHUNK_SIZE) {
				isWorthy = 1;
				omp_set_lock(current_lock_ptr);	
			}
			else {
				isWorthy = omp_test_lock(current_lock_ptr);			
			}
		}

		if (isWorthy) {

			// precisely check remaining
			current_remaining = current_work_queue_ptr->remaining;

			if (dest_work_queue_ptr == NULL) {
				// initialize
				dest_work_queue_ptr = current_work_queue_ptr;
				maxRemaining = current_remaining;

				last_lock_ptr = current_lock_ptr;
			}
			else {
				// if the remaining of current victim greater than last victim
				// unlock last and swap the address of current and last
				if (current_remaining >= maxRemaining) {
					dest_work_queue_ptr = current_work_queue_ptr;
					maxRemaining = current_remaining;

					omp_unset_lock(last_lock_ptr);
					last_lock_ptr = current_lock_ptr;
				}
				else {
					// otherwise, unlock current
					omp_unset_lock(current_lock_ptr);
				}
			}
		}
	}

	// Steal Work

	if (maxRemaining > 0) {

		// calculate chunk size to be moved
		int temp_chunkSize = (int)((double)maxRemaining * ((double)1/(double)nthreads));
		int temp_hi = dest_work_queue_ptr->assignedHi;
		int temp_lo =  temp_hi - temp_chunkSize;

		int currentHi = dest_work_queue_ptr->currentHi;
		if (temp_lo <= currentHi) {
			temp_lo = currentHi;	
		}

		// decrease amount of work in the target
		dest_work_queue_ptr->assignedHi = temp_lo;
		dest_work_queue_ptr->remaining = temp_lo - currentHi;

		*lo = temp_lo;
		*hi = temp_hi;
	}
	else {
		*lo = -1;
		*hi = -1;
	}
	
	if (last_lock_ptr != NULL) {
		omp_unset_lock(last_lock_ptr);
	}
}

void setMinChunkSize(int minChunkSize) {

	/*
	 * set minimum chunk size (DEFAULT_MIN_CHUNK_SIZE = 1)
	 */
	 
	MIN_CHUNK_SIZE = minChunkSize;
}

void initializeLocalWorkQueue(int myid,int nthreads,int lo,int hi,int * startLo,int * startHi,LocalWorkQueue ** my_work_queue_ptr_ptr) {

	/*
	 * initialize local work queue
	 */

	// id
	local_work_queue.myid = myid;

	// lock
	omp_init_lock(&(local_work_queue.mylock));

	// my assigned work
	local_work_queue.assignedLo = lo;
	local_work_queue.assignedHi = hi;

	// modification to MIN_CHUNK_SIZE if inappropriate
	if (MIN_CHUNK_SIZE > hi - lo) {
		MIN_CHUNK_SIZE = hi - lo;
	}

	// my work in hand
	local_work_queue.currentLo = lo;

	// calculate next chunk size
	int temp_chunkSize = (int)((double)(hi-lo) * ((double)1/(double)nthreads));
	int chunkSize = temp_chunkSize <= MIN_CHUNK_SIZE ? MIN_CHUNK_SIZE : temp_chunkSize;

	local_work_queue.currentHi = (lo+chunkSize) > hi? hi : (lo+chunkSize);

	// remaining
	local_work_queue.remaining = hi - lo;

	// start lo and hi
	*startLo = local_work_queue.currentLo;
	*startHi = local_work_queue.currentHi;

	// return a pointer to local work queue
	*my_work_queue_ptr_ptr = &local_work_queue;
}

void schedule(int startLo,int startHi,int nthreads,LocalWorkQueue ** global_work_queue,void (*func)(int,int))
{

	/**************************************************************/
	/************************Do Local Work*************************/	
	/**************************************************************/
	int currentLo = startLo;
	int currentHi = startHi;

	while (true) {

		if (currentLo == currentHi) {		
			break;
		}

		// do a chunk
		func(currentLo,currentHi);
		
		// update local work queue
		updateLocalWorkQueue(nthreads,&currentLo,&currentHi);
		
	}

	/**************************************************************/
	/************************Do Extra Work*************************/	
	/**************************************************************/
	int newWorkLo = -1;
	int newWorkHi = -1;

	while (true) {

		// get information about all victims and steal work from one selected victim thread
		stealWork(nthreads,global_work_queue,&newWorkLo,&newWorkHi);

		if (newWorkLo == -1 && newWorkHi == -1) {
			break;
		}
		else {
			// do extra chunk
			func(newWorkLo,newWorkHi);
		}

	}
}
