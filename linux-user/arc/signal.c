#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "signal-common.h"
#include "linux-user/trace.h"
#include "target_ptrace.h"

void setup_rt_frame(int sig, struct target_sigaction *ka, target_siginfo_t *info, target_sigset_t *set, CPUArchState *env)
{
}

long do_rt_sigreturn(CPUArchState *env)
{
    return 0;
}
