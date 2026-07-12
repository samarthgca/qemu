#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

void helper_raise_exception(CPUArchState *env, int excp)
{
    CPUState *cs = env_cpu(env);
    cs->exception_index = excp;
    cpu_loop_exit(cs);
}


