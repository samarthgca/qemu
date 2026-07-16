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

void arc_translate_init(void);
void arc_translate_code(CPUState *cs, TranslationBlock *tb, int *max_insns, vaddr pc, void *host_pc);

typedef struct CPUArchState {
	uint32_t r[32];
	uint32_t pc;
}CPUArcState;

struct ArchCPU {
    CPUState parent_obj;
    CPUArchState env;
};

#endif
