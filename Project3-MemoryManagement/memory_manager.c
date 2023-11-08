
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/pgtable.h>

// For Testcase 3:
// Change timer_interval_ns to 30e9.
// Change call back func return to HRTIMER_NORESTART

// input params
static int pid =-1;
module_param(pid, int, 0);

struct hrtimer hr_timer;
//struct hrtimer hr_timer_no_restart;

int rss;
int wss;
int swap;

// Test3: change to 30e9
unsigned long timer_interval_ns = 30e9; // timer_interval = 10s

int ptep_test_and_clear_young(struct vm_area_struct* vma, unsigned long addr, pte_t *ptep){
  int ret = 0;
  if(pte_young(*ptep))
    ret = test_and_clear_bit(_PAGE_BIT_ACCESSED, (unsigned long *) &ptep->pte);
  return ret;
  
}


static void PageWalker(struct task_struct* task, struct vm_area_struct *vma, unsigned long address){

  pgd_t *pgd;
  p4d_t *p4d;
  pmd_t *pmd;
  pud_t *pud;
  pte_t *ptep, pte;
  
    struct mm_struct *mm = task->mm;

  pgd = pgd_offset(mm, address);
  if(pgd_none(*pgd) || pgd_bad(*pgd)){
    goto done;
  }

  p4d = p4d_offset(pgd, address);
  if(p4d_none(*p4d) || p4d_bad(*p4d)){
    goto done;
  }

  pud = pud_offset(p4d, address);
  if(pud_none(*pud) || pud_bad(*pud)){
    goto done;
  }

  pmd = pmd_offset(pud, address);
  if(pmd_none(*pmd) || pmd_bad(*pmd)){
    goto done;
  }

  ptep = pte_offset_map(pmd, address);
  if(!ptep){
    goto done;
  }
  pte = *ptep;

  // Output:
  
  // check if present bit set
  if(pte_present(pte)){
    rss++;

    // check if accessed bit set
    if(ptep_test_and_clear_young(vma, address, &pte)){
      wss++;
    }
    
  }
  else{
    swap++;
  }
  
  // pte_unmap(ptep);
  
 done:
  return;

}


enum hrtimer_restart timer_callback(struct hrtimer *timer){
  
  ktime_t currtime, interval;
  
  struct vm_area_struct* vma;
  struct task_struct* task;
  printk("---Timer Calling---");

  // Reset counts
  rss = 0;
  wss = 0;
  swap = 0;

  // Timer Settings
  currtime = ktime_get();
  interval = ktime_set(0, timer_interval_ns);
  hrtimer_forward(timer, currtime, interval);
  
  // Measuring Set Sizes
  // find task struct of the PID
  for_each_process(task){
    if(task->pid == pid)
      break;
  }

  // printk("--Found Task[%d]", pid);

  vma = task->mm->mmap;

  // iterate through all VMAs
  while(vma!=NULL){
    unsigned long i = vma->vm_start;

    while(i < vma->vm_end){
      PageWalker(task, vma, i);
      i += PAGE_SIZE;
    }
    
    
    vma = vma->vm_next;
  }

  printk("---Timer Calledback---");
  
  // page size = 4 KB
  printk("PID [%d]: RSS=[%d] KB, SWAP=[%d] KB, WSS=[%d] KB", pid, rss*4, swap*4, wss*4);

  // test3: HRTIMER_NORESTART
  return HRTIMER_NORESTART;
}


void timer_init(void){
  
  ktime_t ktime = ktime_set(0, timer_interval_ns);
  // ktime_t ktime_no_restart = ktime_set(0, timer_interval_ns);
  printk("---Timer Initiated---");

  hrtimer_init( &hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  hr_timer.function = &timer_callback;
  hrtimer_start( &hr_timer, ktime, HRTIMER_MODE_REL);

}


int memory_init(void){

  printk("CSE330 Project-2 Kernel Module Inserted");
  
  timer_init();
  
  return 0;
  
}

void memory_exit(void){

  // Stop Timmers
  hrtimer_cancel(&hr_timer);
  // hrtimer_cancel(&hr_timer_no_restart);

   printk("---Exiting---");
  
}


module_init(memory_init);
module_exit(memory_exit);
MODULE_LICENSE("GPL");
