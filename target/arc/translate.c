#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/osdep.h"
#include "exec/translator.h"
#include "tcg/tcg-op.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "qemu/qemu-print.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

typedef struct DisasContext {
    DisasContextBase base;
} DisasContext;

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#include "decode-insns.c.inc"
#undef  HELPER_H

static TCGv_i32 cpu_pc;
static TCGv_i32 cpu_regs[32];
void arc_translate_init(void)
{
    static const char * const regnames[] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    };
    cpu_pc = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, pc), "pc");
    for (int i = 0; i < 32; i++) 
    {
        cpu_regs[i] = tcg_global_mem_new_i32(tcg_env,offsetof(CPUArcState, r[i]),regnames[i]);
    }

}

static bool trans_MOV(DisasContext *dc, arg_mov *a)
{
    tcg_gen_mov_i32(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_MOV_U6(DisasContext *dc, arg_mov_u6 *a)
{
    tcg_gen_movi_i32(cpu_regs[a->b], a->u);
    return true;
}

static bool trans_MOV_S12(DisasContext *dc, arg_mov_s12 *a)
{
    tcg_gen_movi_i32(cpu_regs[a->b], a->s);
    return true;
}

static bool trans_FLAG_U6(DisasContext *dc, arg_flag *a)
{
    if (a->u & 1) {
        gen_helper_halt(tcg_env);
        dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool trans_ADD(DisasContext *dc, arg_add *a)
{
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_ADD_U6(DisasContext *dc, arg_add_u6 *a)
{
    tcg_gen_addi_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    return true;
}

static bool trans_ADD_S12(DisasContext *dc, arg_add_s12 *a)
{
    tcg_gen_addi_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    return true;
}

static bool trans_MPY(DisasContext *dc, arg_mpy *a)
{
    tcg_gen_mul_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_MPY_u6(DisasContext *dc, arg_mpy_u6 *a)
{
    tcg_gen_muli_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    return true;
}

static bool trans_MPY_s12(DisasContext *dc, arg_mpy_s12 *a)
{
    tcg_gen_muli_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    return true;
}

static bool trans_SUB(DisasContext *dc, arg_SUB *a)
{
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_SUB_u6(DisasContext *dc, arg_SUB_u6 *a)
{
    tcg_gen_subi_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    return true;
}

static bool trans_SUB_s12(DisasContext *dc, arg_SUB_s12 *a)
{
    tcg_gen_subi_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    return true;
}

static void arc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    uint16_t insn_hi = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next, MO_LE);
    uint16_t insn_lo = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next + 2, MO_LE);
    uint32_t insn = (insn_hi << 16) | insn_lo;
    if (!decode(dc, insn)) {
          gen_helper_halt(tcg_env);
          dc->base.is_jmp = DISAS_NORETURN;
      }
    dc->base.pc_next += 4;
}

static void arc_tr_init_disas_context(DisasContextBase *db, CPUState *cs) { }

static void arc_tr_tb_start(DisasContextBase *db, CPUState *cs)
{

}

static void arc_tr_insn_start(DisasContextBase *db, CPUState *cs)
{
    DisasContext *dc = container_of(db, DisasContext, base);
    tcg_gen_insn_start(dc->base.pc_next, 0, 0);
}
static void arc_tr_tb_stop(DisasContextBase *db, CPUState *cs) 
{ 
    DisasContext *dc = container_of(db, DisasContext, base);
    tcg_gen_movi_i32(cpu_pc, dc->base.pc_next);
    tcg_gen_exit_tb(NULL, 0);

}

static const TranslatorOps arc_tr_ops = { 
    .init_disas_context = arc_tr_init_disas_context,
    .tb_start           = arc_tr_tb_start,
    .insn_start         = arc_tr_insn_start,
    .translate_insn     = arc_tr_translate_insn,
    .tb_stop            = arc_tr_tb_stop,};

void arc_translate_code(CPUState *cs, TranslationBlock *tb,
                             int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext dc = { };
    translator_loop(cs, tb, max_insns, pc, host_pc, &arc_tr_ops, &dc.base);
}