/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: process switching, communication and termination
 * system calls are basically inter-process communication
 */


#include "egos.h"
#include "grass.h"

static void proc_yield();
static void (*kernel_entry)();

int proc_curr_idx;
struct process proc_set[MAX_NPROCESS];

void ctx_entry() {
    kernel_entry();
    /* switch back to user application */
    void* tmp;
    ctx_switch(&tmp, proc_set[proc_curr_idx].sp);
}

void intr_entry(int id) {
    if (id == INTR_ID_TMR) {
        /* switch to kernel stack and call kernel_entry */
        kernel_entry = proc_yield;
        ctx_start(&proc_set[proc_curr_idx].sp, (void*)KERNEL_STACK_TOP);
    } else if (id == INTR_ID_SOFT) {
        /* software interrupt for system call */
        struct syscall *sc = (struct syscall*)SYSCALL_ARGS_BASE;
        sc->type = SYS_UNUSED;
        *((int*)RISCV_CLINT0_MSIP_BASE) = 0;

        INFO("Got system call #%d with arg %d", sc->type, sc->args.exit.status);        
    } else {
        FATAL("Got unknown interrupt #%d", id);
    }
}

static void proc_yield() {
    int mepc;
    __asm__ volatile("csrr %0, mepc" : "=r"(mepc));
    if (mepc < VADDR_START) {
        /* IO may be busy; do not switch context */
        timer_reset();
        return;
    }

    int proc_next_idx = -1;
    for (int i = 1; i <= MAX_NPROCESS; i++) {
        int tmp_next = (proc_curr_idx + i) % MAX_NPROCESS;
        if (proc_set[tmp_next].status == PROC_READY ||
            proc_set[tmp_next].status == PROC_RUNNABLE) {
            proc_next_idx = tmp_next;
            break;
        }
    }

    if (proc_next_idx == proc_curr_idx) {
        timer_reset();
        return;
    }

    int curr_pid = PID(proc_curr_idx);
    int next_pid = PID(proc_next_idx);
    int next_status = proc_set[proc_next_idx].status;

    earth->mmu_switch(next_pid);
    proc_set_running(next_pid);
    proc_set_runnable(curr_pid);
    proc_curr_idx = proc_next_idx;

    if (next_status == PROC_READY) {
        timer_reset();
        __asm__ volatile("csrw mepc, %0" ::"r"(VADDR_START));
        __asm__ volatile("mret");
    } else if (next_status == PROC_RUNNABLE) {
        timer_reset();
        void* tmp;
        ctx_switch(&tmp, proc_set[proc_curr_idx].sp);
    }

    FATAL("Reach the end of proc_yield without switching to any process");
}

