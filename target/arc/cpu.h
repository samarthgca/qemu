#ifndef ARC_CPU_H
#define ARC_CPU_H
#define CPU_RESOLVING_TYPE TYPE_ARC_CPU
#define MMU_USER_IDX 0
#define MMU_KERNEL_IDX 1
#define EXCP_SYSCALL 1
#define EXCP_ILLEGAL 2

#include "qemu/typedefs.h"
#include "cpu-qom.h"
#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"

void arc_translate_code(CPUState *cs, TranslationBlock *tb, int *max_insns, vaddr pc, void *host_pc);

struct ArcCPUClass {
    CPUClass parent_class;
};

struct CPUArchState {
	uint32_t r[32];
	uint32_t pc;
};

struct ArchCPU {
    CPUState parent_obj;
    CPUArchState env;
};

void arc_translate_init(void);

int arc_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n);
int arc_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);
void arc_cpu_dump_state(CPUState *cs, FILE *f, int flags);

#endif
