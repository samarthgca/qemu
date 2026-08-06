#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "exec/translation-block.h"
#include "fpu/softfloat-helpers.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/tcg.h"

static TCGTBCPUState arc_get_tb_cpu_state(CPUState *cs)
{
    CPUArchState *env = cpu_env(cs);
    uint64_t cs_base = 0;
    uint64_t lend_dist = env->lp_end - (env->pc & -(1u << TARGET_PAGE_BITS));
    if (lend_dist < (1u << TARGET_PAGE_BITS) + 8) {
        uint64_t lbeg_off = env->lp_end - env->lp_start;
        cs_base = lend_dist & ARC_CSBASE_LEND_MASK;
        if (lbeg_off < 256) {
            cs_base |= (lbeg_off << ARC_CSBASE_LBEG_OFF_SHIFT) & ARC_CSBASE_LBEG_OFF_MASK;
        }
    }
    return (TCGTBCPUState){ .pc = env->pc, .cs_base = cs_base };
}

static int arc_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return MMU_USER_IDX;
}

static void arc_restore_state_to_opc(CPUState *cs, const TranslationBlock *tb,
                                      const uint64_t *data)
{
    CPUArchState *env = cpu_env(cs);
    env->pc = data[0];
}

static const TCGCPUOps arc_tcg_ops = {.mmu_index = arc_cpu_mmu_index, .get_tb_cpu_state = arc_get_tb_cpu_state, .initialize     = arc_translate_init,.translate_code = arc_translate_code, .restore_state_to_opc = arc_restore_state_to_opc,};

static void arc_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    Error *local_err = NULL;
    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_init_vcpu(cs);
    cpu_reset(cs);
}

static void arc_cpu_set_pc(CPUState *cs, vaddr value)
{
    CPUArchState *env = cpu_env(cs);
    env->pc = value;
}


static ObjectClass *arc_cpu_class_by_name(const char *cpu_model)
{
    return object_class_by_name(TYPE_ARC_CPU);
}

static DeviceRealize arc_parent_realize;

static void arc_disas_set_info(const CPUState *cpu, disassemble_info *info)
{
    info->endian = BFD_ENDIAN_LITTLE;
    info->print_insn = print_insn_arc;
}

static void arc_cpu_class_init(ObjectClass *oc, const void *data)
{
    CPUClass *cc = CPU_CLASS(oc);                                                                                                          
    cc->class_by_name = arc_cpu_class_by_name; 
    cc->tcg_ops = &arc_tcg_ops;
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_parent_realize(dc, arc_cpu_realizefn, &arc_parent_realize);
    cc->set_pc = arc_cpu_set_pc;
    cc->disas_set_info = arc_disas_set_info;

}

static void arc_cpu_initfn(Object *obj)
{
    
}

static const TypeInfo arc_cpus_type_infos[] = {
    {
        .name = TYPE_ARC_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(ArchCPU),
        .instance_init = arc_cpu_initfn,
        .class_size = sizeof(CPUClass),
        .class_init = arc_cpu_class_init,
    },
  };

DEFINE_TYPES(arc_cpus_type_infos)
