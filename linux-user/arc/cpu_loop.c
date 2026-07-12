#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "user/cpu_loop.h"
#include "signal-common.h"

void cpu_loop(CPUArchState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

        switch (trapnr) {
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case EXCP_SYSCALL:
            env->r[0] = do_syscall(env, env->r[8], env->r[0], env->r[1], env->r[2], env->r[3], env->r[4], env->r[5], 0, 0);
            break;
        case EXCP_ILLEGAL:
            fprintf(stderr, "ILLEGAL INSN at PC=0x%x\n", env->pc);
            exit(1);
        case EXCP_INTERRUPT:
            break;
        case 0:
            fprintf(stderr, "cpu_exec returned 0, PC=0x%x\n", env->pc);
            exit(0);
        default:
            fprintf(stderr, "unhandled exception: trapnr = %d (0x%x)\n", trapnr, trapnr);
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUArchState *env = cpu_env(cs);
    env->r[28] = info->start_stack;  /* SP = r28 */
    env->r[0]  = 0;
}
