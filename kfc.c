#include <assert.h>
#include <sys/types.h>
#include <ucontext.h>
#include <stdlib.h>
#include "kfc.h"
#include "queue.h"
#include "valgrind.h"
static tid_t blocker;
static tid_t bFor;
static queue_t conQ;
static int inited = 0;
static tid_t current;
static ucontext_t switchCon;
struct conID{
	ucontext_t cont;
	int full;
	int ID;
	void *retVal;
	int running;
	tid_t bFor;
};
static struct conID cons[KFC_MAX_THREADS];

/**
 *
 *
 *
 */
void
context_switch(){
struct conID *parent;
parent = &cons[current];
	

struct conID *temp;
temp = queue_dequeue(&conQ);
current = temp->ID;
cons[current].running = 1;
swapcontext(&parent->cont, &temp->cont);
}

void
my_func(void *(*start_func)(void *), void *arg){
kfc_exit(start_func(arg));
}


/**
 * Initializes the kfc library.  Programs are required to call this function
 * before they may use anything else in the library's public interface.
 *
 * @param kthreads    Number of kernel threads (pthreads) to allocate
 * @param quantum_us  Preemption timeslice in microseconds, or 0 for cooperative
 *                    scheduling
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_init(int kthreads, int quantum_us)
{
	
	assert(!inited);
	size_t switchstack_size = KFC_DEF_STACK_SIZE;
	caddr_t switchstack = malloc(switchstack_size);
	VALGRIND_STACK_REGISTER(switchstack, switchstack + switchstack_size);






	getcontext(&switchCon);
	switchCon.uc_stack.ss_sp = switchstack;
	switchCon.uc_stack.ss_size = switchstack_size;
	makecontext(&switchCon, context_switch, 0);

	queue_init(&conQ);
	cons[0].full = 1;
	cons[0].ID = 0;
	cons[0].running = 1;
	cons[0].bFor = -1;
	current = 0;
	inited = 1;
	return 0;
}
/**
 * Cleans up any resources which were allocated by kfc_init.  You may assume
 * that this function is called only from the main thread, that any other
 * threads have terminated and been joined, and that threading will not be
 * needed again.  (In other words, just clean up and don't worry about the
 * consequences.)
 *
 * I won't test this function, since it wasn't part of the original assignment;
 * it is provided as a convenience to you if you are using Valgrind to test
 * (which I heartily encourage).
 */
void
kfc_teardown(void)
{
	assert(inited);

	inited = 0;
}

/**
 * Creates a new user thread which executes the provided function concurrently.
 * It is left up to the implementation to decide whether the calling thread
 * continues to execute or the new thread takes over immediately.
 *
 * @param ptid[out]   Pointer to a tid_t variable in which to store the new
 *                    thread's ID
 * @param start_func  Thread main function
 * @param arg         Argument to be passed to the thread main function
 * @param stack_base  Location of the thread's stack if already allocated, or
 *                    NULL if requesting that the library allocate it
 *                    dynamically
 * @param stack_size  Size (in bytes) of the thread's stack, or 0 to use the
 *                    default thread stack size KFC_DEF_STACK_SIZE
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_create(tid_t *ptid, void *(*start_func)(void *), void *arg,
		caddr_t stack_base, size_t stack_size)
{
	assert(inited);
	ucontext_t newCon;
	//ucontext_t swapCon;
	getcontext(&newCon);
	if(stack_base == NULL){
		if(stack_size == 0) stack_size = KFC_DEF_STACK_SIZE;
		stack_base = malloc(stack_size);
		VALGRIND_STACK_REGISTER(stack_base, stack_base + stack_size);
	}
	
	newCon.uc_stack.ss_sp = stack_base;
	newCon.uc_stack.ss_size = stack_size;
	newCon.uc_link = &switchCon;
	int i = 0;
	for(; i < KFC_MAX_THREADS; i++){
		if(cons[i].full == 0) break;
	}
	cons[i].cont = newCon;
	cons[i].ID = i;
	cons[i].full = 1;
	cons[i].running = 1;
	cons[i].bFor = -1;
	*ptid = i;


	makecontext(&cons[i].cont, (void (*)())my_func, 2, (void (*)())start_func, arg);

	//struct conID *parent;
	//parent = &cons[current];
	
//	queue_enqueue(&conQ, parent);
//	current = i;
//	DPRINTF("got here");
	queue_enqueue(&conQ, &cons[i]);
//	swapcontext(&parent->cont, &switchCon);
	
	return 0;
}

void
kfc_exit(void *ret)
{
	assert(inited);
	cons[current].retVal = ret;
	cons[current].running = 0;
	for(int i = 0; i < KFC_MAX_THREADS; i++){
		if(cons[i].bFor == current){
		queue_enqueue(&conQ, &cons[blocker]);
		cons[i].bFor = -1;
		break;
		}
	}
	//struct conID *temp;
	//temp = queue_dequeue(&conQ);
	//current = temp->ID;
	//setcontext(&temp->cont);
	context_switch();
}

int
kfc_join(tid_t tid, void **pret)
{
	assert(inited);
//	DPRINTF("Thread %d: is trying to join on %d which is in state %d\n",current,tid,cons[tid].running);
	if(cons[tid].running == 1){
		cons[current].bFor = tid;
		context_switch();
	}
//	DPRINTF("Thread is returning %d\n", cons[tid].retVal);
	free(cons[tid].cont.uc_stack.ss_sp);
	*pret = cons[tid].retVal;
	return 0;
}

/**
 * Returns a small integer which identifies the calling thread.
 *
 * @return Thread ID of the currently executing thread
 */
tid_t
kfc_self(void)
{
	assert(inited);
	return current;
}

/**
 * Causes the calling thread to yield the processor voluntarily.  This may often
 * result in another thread being scheduled, but it does not preclude the
 * possibility of the same caller continuing if re-chosen by the scheduler.
 */
void
kfc_yield(void)
{
	assert(inited);
	struct conID *hold;
	hold = &cons[current]; 
	
	struct conID *temper;
	queue_enqueue(&conQ, hold);
	temper = queue_dequeue(&conQ);
	current = temper->ID;
	swapcontext(&hold->cont, &temper->cont);
}


int
kfc_sem_init(kfc_sem_t *sem, int value)
{
	assert(inited);
	sem->val = value;
	return 0;
}
//release
int
kfc_sem_post(kfc_sem_t *sem)
{
	assert(inited);
	//sem->val++;
	if(sem->val <= 0){
		queue_enqueue(&conQ, &cons[sem->blocker]);
		//wakeup(p)
	}
	return 0;
}
//aquire
int
kfc_sem_wait(kfc_sem_t *sem)
{
	assert(inited);
	sem->val--;
	if(sem->val < 0){
		sem->blocker = current;
		context_switch();
	}
	return 0;
}

void
kfc_sem_destroy(kfc_sem_t *sem)
{
	assert(inited);
}
