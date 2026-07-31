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
                abi_long ret = do_syscall(env, env->r[8], env->r[0], env->r[1], env->r[2], env->r[3], env->r[4], env->r[5],  0, 0);
                env->r[0] = ret;
                fprintf(stderr, "GOT SYSCALL, num=%d\n", env->r[8]);
                break;
            }
	        default:
                g_assert_not_reached();
        }
    }
}

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUArchState *env = cpu_env(cs);
    env->pc = info->entry;
}
