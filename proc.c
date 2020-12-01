#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;
int WAIT_LIMIT[5] = {84, 83, 82, 81, 80};

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid()
{
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

#ifdef PBS
  p->priority = DEF_PRIO; // setting default prioirty
#endif
  if (!p || p->pid == 0)
  {
    ;
  }
  else
  {
#ifdef MLFQ
    pushBack(MAX_PRIORITY_Q, p);
    p->latestQTime = ticks;
#endif
    p->ctime = ticks;
    p->rtime = 0;
    p->etime = -1;
  }
  return p;
}

int getTicks(struct proc *currp) {
    return ticks - currp->latestQTime;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}


int getQId(struct proc *currp) {
    return currp->stat.allotedQ[0];
}



int preemption(int prio, int checkSamePrio)
{
    acquire(&ptable.lock);

    for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state != RUNNABLE)
            continue;
#ifdef MLFQ
        int q = getQId(p);
        if (q == NO_Q_ALLOT)
            continue;
        if ((q < prio) || (q == prio && checkSamePrio)) {
            release(&ptable.lock);
            return p->pid;
        }
#else
        if ((p->priority < prio) || (p->priority == prio && checkSamePrio)) {
            release(&ptable.lock);
            return 1;
        }
#endif
    }
    release(&ptable.lock);
    return 0;
}



// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

#ifdef PBS
    // if (np && np->pid > 2)
    // {
    //     np->priority = np->pid / 2;
    // } else
        np->priority = DEF_PRIO;
#endif

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->etime = ticks;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

int getQPos(struct proc *currp) {
    return currp->stat.allotedQ[1];
}

void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;
  
#ifdef DEFAULT
    while(1>0)
    {
        // Enable interrupts on this processor.
        sti();
        // Loop over process table looking for process to run.
        acquire(&ptable.lock);
        struct proc *p;
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if (p->state != RUNNABLE)
                continue;
            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;

            swtch(&(c->scheduler), p->context);
            switchkvm();

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
        }
        release(&ptable.lock);
    }
#endif

    while (1>0)
    {
        struct proc *givenPrio = 0;

        // Enable interrupts on this processor.
        sti();
        // Loop over process table looking for process to run.
        acquire(&ptable.lock);

#ifdef FCFS
        struct proc *minTimeP = 0;
        
        for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
            if (p->state == RUNNABLE)
            {
                if (minTimeP)
                {
                    if (p->ctime < minTimeP->ctime)
                        minTimeP = p;
                }
                else
                {
                    minTimeP = p;
                }
            }
        }

        if (minTimeP)
        {
            givenPrio = minTimeP;
            // cprintf("Proc %s pid %d scheduled to run\n", givenPrio->name,givenPrio->pid);
        }
#endif

#ifdef MLFQ
        for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
            if (p && p->state == RUNNABLE)
            {
                if (getQId(p) == NO_Q_ALLOT)
                {
                    cprintf("Process %d\n", p->pid);
                    panic("Should have been alloted in allocproc/wakeup1");
                }
                else
                {
                    if (prioQSize[getQId(p)] == 0)
                    {
                        pushBack(getQId(p), p);
                    }
                }
            }
        }

        for (int i = 0; i < NO_OF_Q; i++)
        {
            while (prioQSize[i])
            {
                struct proc *p = getFront(i);
                if (!p || p->killed || !p->pid || p->state != RUNNABLE)
                {
                    popFront(i);
                    p = 0;
                }
                else 
                {
                    givenPrio = p;
                    break;
                }
            }

            if (givenPrio)
                break;
        }
#endif

#ifdef PBS
        int min_priority = 101;
        for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
            if (p->state == RUNNABLE)
            {
                if (p->priority < min_priority)
                    min_priority = p->priority;
            }
        }

        for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
            if(p->state != RUNNABLE)
              continue;
            if (p->priority != min_priority)
              continue;
            struct proc *givenPrio = p;

            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            c->proc = givenPrio;
            switchuvm(givenPrio);

            givenPrio->state = RUNNING;
            swtch(&(c->scheduler), givenPrio->context);
            switchkvm();
            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;

            // If I got yielded because of higher priority process coming into my way
            int min_priority2 = 101;
            for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
            {
                if (p->state != RUNNABLE)
                  continue;
                if (p->priority < min_priority2)
                    min_priority2 = p->priority;
            }

            if (min_priority > min_priority2)
            {
                break;
            }
                
            
        }
        goto end;

#endif
        if (givenPrio)
        {
            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            c->proc = givenPrio;
            switchuvm(givenPrio);

            if (givenPrio->state != RUNNABLE)
            {
                cprintf("%d\n", givenPrio->state);
                panic("Non runnable process selected for execution\n");
        }

#ifdef MLFQ
            // removing running process from queue
        popFront(getQId(givenPrio));
        givenPrio->stat.num_run++;
        givenPrio->latestQTime = ticks;
#endif

        givenPrio->state = RUNNING;
        swtch(&(c->scheduler), givenPrio->context);

#ifdef MLFQ
            // technically it should be pushing at the back of the same
            // queue if it had not yield
        if (!givenPrio)
            panic("Returning from swtch; alloted process is blank");

        // if process went to sleep or was not able to complete its full
        // time slice, push it to end of same queue
        int queueId = getQId(givenPrio), procTcks = getTicks(givenPrio);

        if ((givenPrio->state == SLEEPING) ||
            (givenPrio->state == RUNNABLE && procTcks > 0 &&
             (procTcks < (1 << queueId))))
        {
            decrease_priority(givenPrio, 1);
        }
        else if(givenPrio->state == RUNNABLE && procTcks == (1 << queueId))
        {
            decrease_priority(givenPrio, 0);
        }
#endif
        switchkvm();
        // Process is done running for now. It should have changed its p->state before coming back.
        c->proc = 0;
    }
#ifdef PBS
  end:
#endif
      release(&ptable.lock);
  }
}

struct proc *getFront(int qId)
{
    if ( !prioQSize[qId] )
    {
        panic("Accessing front of empty queue");
    }
    struct proc *p = prioQ[qId][prioQStart[qId]];
    return p;
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.

void
sched(void)
{

  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;

}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
    {
#ifdef MLFQ
      pushBack(getQId(p), p);
#endif
      p->state = RUNNABLE;
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

void pushBack(int qId, struct proc *p)
{
    if (!p)
    {
        panic("Cannot push back empty proc");
    }
    int bindex = (prioQStart[qId] + prioQSize[qId]) % MAX_PROC_COUNT;;
    for (int i = prioQStart[qId]; i != bindex; i=((i+1) % MAX_PROC_COUNT))
    {
        if (prioQ[qId][i] && prioQ[qId][i]->pid == p->pid)
        {
            return;
        }
    }

    p->stat.allotedQ[0] = qId;
    p->stat.allotedQ[1] = (prioQStart[qId] + prioQSize[qId]) % MAX_PROC_COUNT;
    prioQ[qId][p->stat.allotedQ[1]] = p;
    ++prioQSize[qId];
}

void deleteId(int qId, int Id)
{
    if (!prioQ[qId][Id])
    {
        panic("Already deleted index");
    }

    prioQ[qId][Id] = 0;
    prioQSize[qId]--;
    int bi = (prioQStart[qId] + prioQSize[qId]) % MAX_PROC_COUNT;
    for (int i = Id; i != bi; i=((i+1) % MAX_PROC_COUNT))
    {
        prioQ[qId][i] = prioQ[qId][(i + 1) % MAX_PROC_COUNT];
        prioQ[qId][i]->stat.allotedQ[1] = i;
    }
}

void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.

int waitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
      // Scan through table looking for exited children.
      havekids = 0;
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
          if (p->parent != curproc)
              continue;
          havekids = 1;
          if (p->state == ZOMBIE)
          {
              // Found one.
              pid = p->pid;
              kfree(p->kstack);

              *rtime = p->rtime;
              p->etime = ticks;
              *wtime = p->etime - p->ctime - p->rtime;

              p->kstack = 0;
              freevm(p->pgdir);
              p->pid = 0;
              p->parent = 0;
              p->name[0] = 0;
              p->killed = 0;
              p->state = UNUSED;
              release(&ptable.lock);
              return pid;
          }
      }

      // No point waiting if we don't have any children.
      if (!havekids || curproc->killed)
      {
          release(&ptable.lock);
          return -1;
      }

      // Wait for children to exit.  (See wakeup1 call in proc_exit.)
      sleep(curproc, &ptable.lock);  // DOC: wait-sleep
  }
}


int setPriority(int newPriority, int pid)
{
  if (newPriority < 0 || newPriority > 100)
        return -1;
  int oldPriority = 100;
  for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      oldPriority = p->priority;
      p->priority = newPriority;
    }
  }
  cprintf("Process pid %d with old priority %d is set to new priority %d.\n", pid, oldPriority, newPriority);
  return oldPriority;
}

int ps()
{
  static char *states[] = {
  [UNUSED]    "UNUSED  ",
  [EMBRYO]    "EMBRYO  ",
  [SLEEPING]  "SLEEPING",
  [RUNNABLE]  "RUNNABLE",
  [RUNNING]   "RUNNING ",
  [ZOMBIE]    "ZOMBIE  "
  };

  cprintf("Pid \t Priority \t State     \t r_time \t w_time \t\t n_run \t\t cur_q \t q0 \t q1 \t q2 \t q3 \t q4\n\n");
  for(struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {

    if(strncmp(states[p->state],"RUNNING ",8)==0 || strncmp(states[p->state],"RUNNABLE",8)==0 ||
     strncmp(states[p->state],"SLEEPING",8)==0 || strncmp(states[p->state],"ZOMBIE  ",8)==0)
    {
      cprintf("%d \t %d \t \t %s \t %d \t\t %d \t\t\t %d \t\t %d \t %d \t %d \t %d \t %d \t %d\n",
        p->pid,p->priority,states[p->state],p->rtime, ticks - p->rtime - p->ctime, p->stat.num_run ,
        p->stat.allotedQ[0], p->stat.TicksQ[0], p->stat.TicksQ[1], p->stat.TicksQ[2], p->stat.TicksQ[3], p->stat.TicksQ[4]);
    }
  }
  return 1;
}

void increase_priority(struct proc *currp)
{
    int queueId = getQId(currp), qPos = getQPos(currp); // getting the qid of queue where process is and its position
    if (queueId < 0 || qPos < 0)
    {
        panic("Invalid queue");
    }
    deleteId(queueId, qPos);

    if (!currp)
        panic("error in current process");
    int dest = queueId; // we set destination as qid
    if (queueId == MAX_PRIORITY_Q) // if already max priority then dont change queue
    {
        pushBack(queueId, currp);
    }
    else
    {
        dest--; // else move it to higher priority
        pushBack(queueId - 1, currp);
    }
}

// #ifdef MLFQ
void updateAging()
{
    if (cpuid() != 0)
        panic("Must be called from cpu0 only");

    ticks++;

#ifdef MLFQ
    acquire(&ptable.lock);
    for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if (!p || p->killed || p->pid == 0)
            continue;

        if (p->state == RUNNABLE || p->state == RUNNING)
        {
            int qId = getQId(p);
            int tcks = getTicks(p) + 1;
            if (p->state == RUNNING)
                p->stat.TicksQ[qId]++;

            if (p->state == RUNNABLE && tcks >= WAIT_LIMIT[qId])
            {
                increase_priority(p); // we increase the priority if waitlimit is exceeded
            }
        }
    }
    release(&ptable.lock);
#endif
}

void decrease_priority(struct proc *currp, int retain)
{
    int queueId = getQId(currp);
    if (queueId < 0)
    {
        panic("Invalid queue");
    }

    if (!currp)
        panic("error in current process");

    currp->latestQTime = ticks;
    int dest = queueId;

    if (queueId == NO_OF_Q - 1 || retain)
    {
        pushBack(queueId, currp);
    }
    else
    {
        pushBack(queueId + 1, currp);
        dest++;
    }
}



struct proc *popFront(int qId)
{
    if (!prioQSize[qId])
    {
        panic("Empty stack, cannot pop");
    }

    // DO NOT CHANGE allotted q since they help in memory
    struct proc *p = getFront(qId);
    prioQStart[qId]++;
    if (prioQStart[qId] == MAX_PROC_COUNT)
    {
        prioQStart[qId] = 0;
    }
    prioQSize[qId]--;
    return p;
}


// #endif