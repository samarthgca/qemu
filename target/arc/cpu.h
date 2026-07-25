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
    uint32_t zf;
    uint32_t nf;
    uint32_t cf;
    uint32_t vf;
    uint32_t identity;
    uint32_t status32;
    uint32_t bta;
    uint32_t ecr;
    uint32_t int_vector_base;
    uint32_t aux_user_sp;
    uint32_t eret;
    uint32_t erbta;
    uint32_t erstatus;
    uint32_t efa;
    uint32_t bcr_ver;
    uint32_t bta_link_build;
    uint32_t vecbase_ac_build;
    uint32_t rf_build;
    uint32_t isa_config;
}CPUArcState;

struct ArchCPU {
    CPUState parent_obj;
    CPUArchState env;
};

#endif
