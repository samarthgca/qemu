#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "user/cpu_loop.h"
#include "signal-common.h"

void cpu_loop(CPUArcState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;

    for (;;) {
	cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);
        switch (trapnr) {
            case EXCP_INTERRUPT:
                break;
            case EXCP_SYSCALL: {
                abi_long arg0 = env->r[0];
                abi_long ret; 
                if (env->r[8] == 245) {
                    env->tls_value = arg0;
                    ret = 0;
                } else {
                ret = do_syscall(env, env->r[8], env->r[0], env->r[1], env->r[2], env->r[3], env->r[4], env->r[5], 0, 0);
                }
                env->r[0] = ret;
                fprintf(stderr, "GOT SYSCALL, num=%d arg0=0x%x ret=0x%x\n", env->r[8], arg0, ret);
                break;
            }
	        default:
                g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUArchState *env = cpu_env(cs);
    env->pc = info->entry;
    env->r[28] = info->start_stack;
}
