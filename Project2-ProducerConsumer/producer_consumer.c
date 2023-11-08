#include <linux/init.h>
#include <linux/slab.h>   //for kcalloc
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include<linux/sched.h>
#include <linux/sched/signal.h>

/*
  searches the system for all the processes that belong to a given user and adds their task_struct to the shared buffer.
  The kernel stores all the processes in a circular doubly linked list called task list.
  Each element in the task list is a process descriptor of the type struct task_struct.
  can loop over all tasks by using for_each_process(struct task_struct *p)
  NOTE:
    There is a diffrence between the test script and us, that is due to two factors from the PS cmd
    1. It dose not run at the same time as this module
    2. It gives time in sec but we mesure in ns so we will slowly over count for each process
*/

//declare input args with default values
static int buffSize = 10;
static int prod = 1;
static int cons = 1;
static int uuid = -1;//this is an error value, uuid has no defualt

/*
	when empty is created it is set at the bufferSize
	it exist to track the number of empty slots in the buffer.
	thus if a new buffer is created at size 17, it has 17 empty slots
*/
struct semaphore* empty; //ie. how many slots are open
int get;
/*
   when full is created it is set to 0
   it exist to track the number of slots filled by the producer (net total)
   thus if the producer filled 10 slots and the consumer took 7, full will be 7
*/
struct semaphore* full; //ie. how many slots can be consumed
int put;
struct semaphore* mutex;

struct task_struct** threds;
struct task_struct** B;
unsigned long total_time_elapsed;
int total_proc;
char* tName;

// grab args, using perm = 0 to prevent OS from putting args into live file
//macro is extern so it has to be out here at the top (ie. not in a function)
module_param(buffSize, int, 0);
module_param(prod, int, 0);
module_param(cons, int, 0);
module_param(uuid, int, 0);

/*
  Producer - loops over all active proccesses and write their task sturct to the buffer
  uses -
    task_struct** B: the buffer itself
    int uuid: the user id of the person we are getting proccess of
  arg - 
    char* threadName: the name assigned to this thread (thred arg can only have type void* but we can cast back char*)
*/
static int Producer(void* threadName) {
  struct task_struct* p;
  int num_procs;
  
  printk("[%s] Startting\n", (char*)threadName);//threadName is of type void*, so cast back to char*
  num_procs = 0;//since there will only be one prod it will keep track of how many things it puts into buffer
  for_each_process(p) { // On each iteration, p
    //if kthread_should_stop
    if(kthread_should_stop()) {
      break;//module is closing break
    }
    //if task_struct* is not from uuid
    if(p->cred->uid.val != uuid) {
      continue;// we don't care about other processes
    }

    //down_interruptible is in an if so that if it returns non-zero it kills the thread
    if(down_interruptible(empty)) { //grab semaphore (wait on empty)
      printk("[%s] Breaking on empty\n", (char*)threadName);
      break;
    }
    if(down_interruptible(mutex)) {
      printk("[%s] Breaking on mutex\n", (char*)threadName);
      up(empty);//if mutext gets the kill-sig relese empty
      break;
    }
    if(kthread_should_stop()) {//if forced awake by kill signal   this if block could be deleted if down_interruptible behaved properly
      printk("[%s] Breaking on Tstop\n", (char*)threadName);
      break;//escape crit section
    }
    B[put] = p;//push task_struct* to B
    num_procs++;
    printk("[%s] Produced Item#-%d at buffer index:%d for PID:%d\n", (char*)threadName, num_procs, put, p->pid);
    put = (put + 1) % buffSize;//move fill to next index
    up(mutex);//release mutex
    up(full);//release semaphore (post on full)
  }
  
  printk("[%s] Exitting", (char*)threadName);
  if(threadName){ //if tName has not been freed
    kfree(threadName);//free it
  }
  //all proc's are computed end thread
  return 0;
}
  
  
/*
  Consumer - Reads from buffer and prints relivent info until signled to die
  args -
    buffSize: size of buffer the is being read from
    B: the buffer itself
*/
static int Consumer(void* threadName) {
  printk("[%s] Startting", (char*)threadName);
  //wait(full) // waits till full is not 0, (0 full means its empty and nothing can be removed) then performs wait which does -1
  //wait(mutex) // waits for the critical section to be free, or the lock to be unlocked
      
  //now in critical section, things can be added or removed    
  while(!kthread_should_stop()) {
    struct task_struct* c;
    unsigned long proc_time;
    int h;
    int m;
    int s;
    
    /*
    if B is not empty
      read from B and print format
      "[<Consumer-thread-name>] Consumed Item#-<Item-Num> on buffer index: <buffer-index> PID:<PID consumed>  Elapsed Time- <Elapsed time of the consumed PID in HH:MM:SS>"
      add time of proc to total_time_elapsed
    */
    if(down_interruptible(full)) {
      printk("[%s] Breaking on full", (char*)threadName);
      break;
    }
    if(down_interruptible(mutex)) {
      printk("[%s] Breaking on mutex", (char*)threadName);
      up(full);//if mutext get the kill-sig relese full
      break;
    }
    if(kthread_should_stop()) {//if forced awake by kill signal   this if block could be deleted if down_interruptible behaved properly
      printk("[%s] Breaking on Kstop", (char*)threadName);
      break;//escape crit section
    }
    //get the struct* from the buffer
    c = B[get];
    proc_time = ktime_get_ns() - c->start_time;
    total_time_elapsed += proc_time;//add p's time to total time
    total_proc++;//count the users's proc
    //calc time in HH:MM:SS
    s = (int)(proc_time / (unsigned long)1000000000);
    h = s/3600;
    s -= h*3600;
    m = s/60;
    s -= m*60;
    printk("[%s] Consumed Item#-%d on buffer index:%d PID:%d Elapsed Time-%d:%d:%d", (char*)threadName, total_proc, get, c->pid, h, m, s);
    get = (get + 1) % buffSize;
    up(mutex);
    up(empty);
  }
  
  printk("[%s] Exitting", (char*)threadName);
  if(threadName){
    kfree(threadName);
  }
  return 0;
}
    
    
/*
  module init - sets up all vars and threads when module is loaded
  module is expecting:
      buffSize: The buffer size
      prod: number of producers (0 or 1)
      cons: number of consumers (a non-negative number).
      uuid: The UID of the user
*/
int producer_consumer_init(void) {
	int i;
  
  printk("producer_consumer Starting...\n");
  //echo back all cmd args
  printk("buffSize: %d\n", buffSize);
  printk("prod: %d\n", prod);
  printk("cons: %d\n", cons );
  printk("uuid: %d", uuid);
  
//DEAL WITH GLOBAL STUFF
  empty = (struct semaphore*)kmalloc(sizeof(struct semaphore), GFP_KERNEL);
  sema_init(empty, buffSize);
  get = 0;
  full = (struct semaphore*)kmalloc(sizeof(struct semaphore), GFP_KERNEL);
  sema_init(full, 0);
  put = 0; // hold the curr index we are writing to in B
  mutex = (struct semaphore*)kmalloc(sizeof(struct semaphore), GFP_KERNEL);
  sema_init(mutex, 1);
  // Have an array to hold all threds that have been made
  // This way we can find them all when they need to die
  threds = (struct task_struct**)kmalloc((prod + cons)*sizeof(struct task_struct*), GFP_KERNEL);
  // Array holding pointers to proccesses (struct task_struct**)
  B = (struct task_struct**)kmalloc(buffSize*sizeof(struct task_struct*), GFP_KERNEL);
  //check that mem could be allocated
  if(!threds || !B) {
    printk("Could not allocate memory. Stopping");
    return -ENOMEM;
  }
  // holds total time of all porccesses in nanoseconds
  total_time_elapsed = 0;
  // keep track of the number of proc for a user
  total_proc = 0;
//DONE WITH GLOBAL STUFF

  /* 
    This project requires two kernel threads: producer thread and consumer thread. To create and
    start the kernel threads, you can use the kthread_run() function.
  */
  // start threads
  for(i = 0; i < prod; i++) {
    tName = (char*)kmalloc(11*sizeof(char), GFP_KERNEL);//make new str for the threads name
    // Create and run "producer threads"
    sprintf(tName, "pThread-%d", i);//copy formated name to tName
    threds[i] = kthread_run(Producer, (void*)tName, tName);//start thread
    printk("[INIT] Created thread p%d: %p\n", i, threds[i]);
  }

  for(i = 0; i < cons; i++){
    tName = (char*)kmalloc(11*sizeof(char), GFP_KERNEL);
    // Create and run "consumer thread"
    sprintf(tName, "cThread-%d", i);
    threds[prod + i] = kthread_run(Consumer, (void*)tName, tName);
    printk("[INIT] Created thread c%d: %p\n", i, threds[i]);
  }
  printk("All threads started\n");
  // wait for threds to return (may not be needed)
	return 0;
}

/*
  module exit - clean up module on close by stopping all threads it made
*/
void producer_consumer_exit(void) {
  int i;
  int hh;
  int mm;
  int ss;

	printk("poroducer_consumer Exiting...\n");
  //loop over each thread
  for(i = 0; i < (prod + cons); i++){
    printk("[EXIT] killing thread p%d: %p\tState: %d\n", i, threds[i], threds[i]->exit_state);
    if(threds[i]->exit_state <= 0) { //if thread is still running
      //send kill sginal to semaphores. IDK how to do that but we can up all semas to wake threads then send kill
      //TODO something better than this.
      up(full);
      up(empty);
      up(mutex);
      kthread_stop(threds[i]);//send the kill sginal and ?force? wake thread
      printk("[EXIT] Thread p%d killed\n", i);
    } else {
      printk("[EXIT] Thread p%d already dead\n", i);
    }
  }
  //free all kmalloc and kcalloc vars (if they haven't allready been freed by somthing else.IDK what is freeing it)
  if(empty){kfree(empty);}
  if(full){kfree(full);}
  if(mutex){kfree(mutex);}
  if(threds){kfree(threds);}
  if(B){kfree(B);}
  
  //split time from unsigned long ns to ints seconds, minutes, and hours
  ss = (int)(total_time_elapsed / (unsigned long)1000000000);//change ul ns to int s
  hh = ss/3600;//get as much hh from ss
  ss -= hh*3600;//take out hh in s for ss
  mm = ss/60;
  ss -= mm*60;
  //print formated text
  //"The total elapsed time of all processes for UID <uuid> is\t<total_time_elapsed in HH:MM:SS>"
  printk("The total elapsed time of all processes for UID %d is\t%d:%d:%d\n", uuid, hh,mm,ss);
}


module_init(producer_consumer_init);
module_exit(producer_consumer_exit);
MODULE_LICENSE("GPL");