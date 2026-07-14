#ifndef ARC_TARGET_CPU_H
#define ARC_TARGET_CPU_H
static inline void cpu_clone_regs_child(CPUArcState *env, target_ulong newsp, unsigned flags)
{
}

static inline void cpu_clone_regs_parent(CPUArcState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUArcState *env, target_ulong newtls)
{
}

static inline abi_ulong get_sp_from_cpustate(CPUArcState *state)
{
    return 0;
}

#endif
