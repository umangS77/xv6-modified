# Modified xv-6 OS

## Overview

#### Modified the xv6 operating system developed by MIT and added some features like waitx, ps and setPriority syscalls. Scheduling techniques implemented are FCFS(First-Come-First-Serve),PBS(Priority Based Scheduling) and MLFQ(Multi-Level FeedbackQueue).  

### Execution
```
make clean
make qemu-nox SCHEDULER=<SCHEDULER FLAG>
```
where SCHEDULER FLAG can take values: DEFAULT / FCFS / PBS / MLFQ.


## Task 1 : Implemented waitx and ps  

### waitx syscall  

Syntax:
```
time <command>
```
waitx syscall returns the time spent in running and waiting by a particular process. Both
rtime and wtime are given in terms of ticks defined in xv6.  

### ps (user program)  

Syntax:
```
ps
```
ps user program gives information such as pid, priority, state, rtime, wtime, n_run, current_queue, ticks in all queues, etc. about all active processes.  


## Task 2 : Implement FCFS, PBS, MLFQ scheduling policies  

### FCFS (First Come First Serve)  

In FCFS, the process which arrives first is run first. There is no preemption in this scheduling. This scheduling just selects the process from proc table with least start time i.e. the process which arrived earliest and executes it until it finishes. Only then a new process is selected for execution.

### PBS (Priority Based Scheduling)  

In this scheduling, processes are selected based on priority.  
● process with highest priority is selected from proc table.  
● preemption is done if a process with higher property arrives.  
● when there are processes with same priority then "Round Robin" policy is used.
● both the above functionalities are achieved in trap.c by checking if there is a need of preemption in timeToPreempt() function. If there is a need then yield() is called.  
● Default Priority of 60 is assigned to each process  
● Then we find minimum priority by iterating through all processes in ptable.  
● We also have a check within the 2nd loop to ensure no other process with lower priority has come in. If it has, we break out of the 2nd loop, otherwise DEFAULT is executed for the same priority processes.  
● setPriority() calls yield() when the priority of a process becomes lower than its old priority.  

### MLFQ (Multi-level Feedback Queue)  

Processes are allocated different queues. Each queue has a different priority. A RUNNABLE process is selected from the queue with highest priority possible. We check if the wait time of a process has crossed a particular value. If it has then it is promoted to a queue of higher priority to prevent starvation. In the case of MLFQ, yield() is only called if a process has been running for a very long time in a queue (specific number of allowed clock ticks for different queues). If it has, then it is demoted to a queue of lower priority and scheduler is called again.  

### Functions

● struct proc *getFront(int qIdx) - get front process of queue
● struct proc *popFront(int qIdx) - pop from front of the queue
● void pushBack(int qIdx, struct proc *p) - push process into the queue
● void updateStatsAndAging() - takes care of aging of process in MLFQ and shifting between queues.
