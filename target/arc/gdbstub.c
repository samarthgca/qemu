#include "qemu/osdep.h"
#include "cpu.h"
#include "gdbstub/helpers.h"

 



int arc_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ArcCPU *cpu = ARC_CPU(cs);
    CPUArchState *env = &cpu->env;

    if (n < 32) {
        return gdb_get_reg32(mem_buf, env->r[n]);
    } else if (n == 32) {
        return gdb_get_reg32(mem_buf, env->pc);
    }
    return 0;
}

int arc_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ArcCPU *cpu = ARC_CPU(cs);
    CPUArchState *env = &cpu->env;
    uint32_t tmp = ldl_p(mem_buf);

    if (n < 32) {
        env->r[n] = tmp;
        return 4;
    } else if (n == 32) {
        env->pc = tmp;
        return 4;
    }
    return 0;
}
