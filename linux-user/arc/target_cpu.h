#ifndef ARC_TARGET_CPU_H
#define ARC_TARGET_CPU_H

#define TASK_UNMAPPED_BASE  0x40000000
#define ELF_ET_DYN_BASE     0x08000000

static inline void cpu_clone_regs_child(CPUArchState *env,
                                        target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->r[28] = newsp;  /* SP = r28 on ARC */
    }
    env->r[0] = 0;           /* return 0 to child */
}

static inline void cpu_clone_regs_parent(CPUArchState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUArchState *env, target_ulong newtls)
{
    env->r[25] = newtls;     /* TLS register on ARC */
}

static inline abi_ulong get_sp_from_cpustate(CPUArchState *env)
{
    return env->r[28];       /* SP = r28 on ARC */
}

#endif
