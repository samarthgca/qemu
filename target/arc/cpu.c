#include "qemu/osdep.h"
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include "cpu.h"
#include "qom/object.h"
#include "qemu/error-report.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/tcg.h"
#include "exec/translation-block.h"
#include "qemu/qemu-print.h" 
#include "qapi/error.h"

static DeviceRealize parent_realize;
static ResettablePhases parent_phases;

void arc_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    ArcCPU *cpu = ARC_CPU(cs);
    CPUArchState *env = &cpu->env;
    int i;

    qemu_fprintf(f, "PC = %08x\n", env->pc);
    
    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, "R%02d = %08x", i, env->r[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }
}

static ObjectClass *arc_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }
    typename = g_strdup_printf("%s-" TYPE_ARC_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

    if (oc == NULL || !object_class_dynamic_cast(oc, TYPE_ARC_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }

    return oc;
}


static void arc_cpu_set_pc(CPUState *cs, vaddr value)
{
    ArcCPU *cpu = ARC_CPU(cs);
    cpu->env.pc = value;
}

static vaddr arc_cpu_get_pc(CPUState *cs)
{
    ArcCPU *cpu = ARC_CPU(cs);
    return cpu->env.pc;
}

static TCGTBCPUState arc_get_tb_cpu_state(CPUState *cs)
{
    CPUArchState *env = cpu_env(cs);
    return(TCGTBCPUState){
        .pc = env->pc, .flags = 0
    };
}

static void arc_cpu_synchronize_from_tb(CPUState *cs, const TranslationBlock *tb)
{
    ArcCPU *cpu = ARC_CPU(cs);
    cpu->env.pc = tb->pc;
}

static void arc_restore_state_to_opc(CPUState *cs, const TranslationBlock *tb, const uint64_t *data)
{
    ArcCPU *cpu = ARC_CPU(cs);
    cpu->env.pc = data[0];
}



static int arc_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return MMU_USER_IDX;
}

static void arc_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    ArcCPU *cpu = ARC_CPU(cs);

    memset(&cpu->env, 0, sizeof(cpu->env));
}

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

static struct TCGCPUOps arc_tcg_ops = { .initialize = arc_translate_init, .translate_code = arc_translate_code, .get_tb_cpu_state = arc_get_tb_cpu_state, .synchronize_from_tb = arc_cpu_synchronize_from_tb, .restore_state_to_opc = arc_restore_state_to_opc, .mmu_index = arc_cpu_mmu_index,};

static void arc_cpu_class_init(ObjectClass *oc, const void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    cc->class_by_name = arc_cpu_class_by_name;
    cc->dump_state = arc_cpu_dump_state;
    cc->gdb_read_register = arc_cpu_gdb_read_register;
    cc->gdb_write_register = arc_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 33;
    cc->tcg_ops = &arc_tcg_ops;
    cc->set_pc = arc_cpu_set_pc;
    cc->get_pc = arc_cpu_get_pc;  
    resettable_class_set_parent_phases(rc, NULL, arc_cpu_reset_hold, NULL, &parent_phases);
    device_class_set_parent_realize(dc, arc_cpu_realizefn, &parent_realize);


}
    

static void arc_cpu_instance_init(Object *obj)
{
}

static const TypeInfo arc_cpu_type_infos[] = {
    {
        .name = TYPE_ARC_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(ArcCPU),
        .instance_init = arc_cpu_instance_init,
        .class_init = arc_cpu_class_init,
        .abstract = true,
    },
    {
        .name = "any-" TYPE_ARC_CPU, .parent = TYPE_ARC_CPU,
    },
};

DEFINE_TYPES(arc_cpu_type_infos)


