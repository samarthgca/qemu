#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

void HELPER(halt)(CPUArcState *env)
{
    exit(0);
}

typedef struct {
    uint32_t addr;
    size_t offset;
} AuxRegEntry;

static const AuxRegEntry aux_regs[] = {
    { 0x04,  offsetof(CPUArcState, identity) },
    { 0x0A,  offsetof(CPUArcState, status32) },
    { 0x412, offsetof(CPUArcState, bta) },
    { 0x403, offsetof(CPUArcState, ecr) },
    { 0x25,  offsetof(CPUArcState, int_vector_base) },
    { 0x0D,  offsetof(CPUArcState, aux_user_sp) },
    { 0x400, offsetof(CPUArcState, eret) },
    { 0x401, offsetof(CPUArcState, erbta) },
    { 0x402, offsetof(CPUArcState, erstatus) },
    { 0x404, offsetof(CPUArcState, efa) },
    { 0x60,  offsetof(CPUArcState, bcr_ver) },
    { 0x63,  offsetof(CPUArcState, bta_link_build) },
    { 0x68,  offsetof(CPUArcState, vecbase_ac_build) },
    { 0x6E,  offsetof(CPUArcState, rf_build) },
    { 0xC1,  offsetof(CPUArcState, isa_config) },
};

uint32_t HELPER(aex)(CPUArcState *env, uint32_t addr, uint32_t new_val)
{
    if (addr == 0x6) {
        uint32_t old = env->pc;
        env->pc = new_val;
        return old;
    }
    for (int i = 0; i < ARRAY_SIZE(aux_regs); i++) {
        if (aux_regs[i].addr == addr) {
            uint32_t *field = (uint32_t *)((char *)env + aux_regs[i].offset);
            uint32_t old = *field;
            *field = new_val;
            return old;
        }
    }
    return 0;
}
