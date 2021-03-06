// Per-CPU state
struct
cpu
{
    uchar apicid;               // Local APIC ID
    struct context *scheduler;  // swtch() here to enter scheduler
    struct taskstate ts;        // Used by x86 to find stack for interrupt
    struct segdesc gdt[NSEGS];  // x86 global descriptor table
    volatile uint started;      // Has the CPU started?
    int ncli;                   // Depth of pushcli nesting.
    int intena;                 // Were interrupts enabled before pushcli?
    struct proc *proc;          // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

// PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct
context
{
    uint edi;
    uint esi;
    uint ebx;
    uint ebp;
    uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

#ifdef PBS
#define DEF_PRIO 60
#endif

#define NO_OF_Q 5
#define NO_Q_ALLOT -1
#define MAX_PROC_COUNT (int)1e4
// after 10 ticks, process priority is going to increase
#define MAX_PRIORITY_Q 0
// this priority queue holds all runnable process
// it is changed every time scheduler runs

struct 
proc_stat 
{
    int pid;              // PID of each process
    int runtime;          // Use suitable unit of time
    int num_run;          // number of time the process is executed
    int allotedQ[2];      // current assigned queue and position inside it
    int TicksQ[NO_OF_Q];  // ticks keeps getting resetted everytime it is pushed to a new queue
};

struct proc *prioQ[NO_OF_Q][MAX_PROC_COUNT];
int prioQSize[NO_OF_Q];
int prioQStart[NO_OF_Q];


// Per-process state
struct
proc
{
    uint sz;                     // Size of process memory (bytes)
    pde_t *pgdir;                // Page table
    char *kstack;                // Bottom of kernel stack for this process
    enum procstate state;        // Process state
    int pid;                     // Process ID
    struct proc *parent;         // Parent process
    struct trapframe *tf;        // Trap frame for current syscall
    struct context *context;     // swtch() here to run process
    void *chan;                  // If non-zero, sleeping on chan
    int killed;                  // If non-zero, have been killed
    struct file *ofile[NOFILE];  // Open files
    struct inode *cwd;           // Current directory
    char name[16];               // Process name (debugging)
    int ctime;                   // process ka creation time
    int etime;                   // process ka end time
    int rtime;                   // process ka total time
    int priority;                // process priority
    struct proc_stat stat;
    int latestQTime;
};



// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

int preemption(int prio, int checkSamePrio);
void pushBack(int qId, struct proc *p);
struct proc *popFront(int qId);
struct proc *getFront(int qId);
void decrease_priority(struct proc *currp, int retain);
void increase_priority(struct proc *currp);
void updateAging();
void deleteId(int qId, int Id);
int getQId(struct proc *currp);
int getTicks(struct proc *currp);