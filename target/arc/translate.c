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
    bool has_delay_slot;
    TCGv_i32 delay_target;
} DisasContext;

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#include "decode-insns.c.inc"
#include "decode-insns16.c.inc"
#undef  HELPER_H

static TCGv_i32 cpu_pc;
static const int arc_reduced_regs[8] = {0, 1, 2, 3, 12, 13, 14, 15};
static TCGv_i32 cpu_regs[32];
static TCGv_i32 cpu_zf;
static TCGv_i32 cpu_nf;
static TCGv_i32 cpu_cf;
static TCGv_i32 cpu_vf;
#define SP 28
#define blink 31

void arc_translate_init(void)
{
    static const char * const regnames[] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "SP", "r29", "r30", "blink",
    };
    cpu_pc = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, pc), "pc");
    for (int i = 0; i < 32; i++) 
    {
        cpu_regs[i] = tcg_global_mem_new_i32(tcg_env,offsetof(CPUArcState, r[i]),regnames[i]);
    }
    cpu_zf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, zf), "zf");
    cpu_nf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, nf), "nf");
    cpu_cf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, cf), "cf");
    cpu_vf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, vf), "vf");
}

static TCGv_i32 gen_cc_test(int q)
{
    TCGv_i32 taken = tcg_temp_new_i32();
    TCGv_i32 tmp1 = tcg_temp_new_i32();
    TCGv_i32 tmp2 = tcg_temp_new_i32();
    switch (q) {
    case 0x00: /* AL */
        tcg_gen_movi_i32(taken, 1);
        break;
    case 0x01: /* EQ: Z */
        tcg_gen_mov_i32(taken, cpu_zf);
        break;
    case 0x02: /* NE: /Z */
        tcg_gen_xori_i32(taken, cpu_zf, 1);
        break;
    case 0x03: /* PL: /N */
        tcg_gen_xori_i32(taken, cpu_nf, 1);
        break;
    case 0x04: /* MI: N */
       tcg_gen_mov_i32(taken, cpu_nf);
        break;
    case 0x05: /* CS: C */
        tcg_gen_mov_i32(taken, cpu_cf);
        break;
    case 0x06: /* CC: /C */
        tcg_gen_xori_i32(taken, cpu_cf, 1);
        break;
    case 0x07: /* VS: V */
        tcg_gen_mov_i32(taken, cpu_vf);
        break;
    case 0x08: /* VC: /V */
        tcg_gen_xori_i32(taken, cpu_vf, 1);
        break;
    case 0x09: /* GT: (N==V) and /Z */
        tcg_gen_xor_i32(tmp1, cpu_nf, cpu_vf);
        tcg_gen_xori_i32(tmp1, tmp1, 1);
        tcg_gen_xori_i32(tmp2, cpu_zf, 1);
        tcg_gen_and_i32(taken, tmp1, tmp2);
        break;
    case 0x0A: /* GE: N==V */
        tcg_gen_xor_i32(taken, cpu_nf, cpu_vf);
        tcg_gen_xori_i32(taken, taken, 1);
        break;
    case 0x0B: /* LT: N!=V */
        tcg_gen_xor_i32(taken, cpu_nf, cpu_vf);
        break;
    case 0x0C: /* LE: Z or (N!=V) */
        tcg_gen_xor_i32(tmp1, cpu_nf, cpu_vf);
        tcg_gen_or_i32(taken, cpu_zf, tmp1);
        break;
    case 0x0D: /* HI: /C and /Z */
        tcg_gen_xori_i32(tmp1, cpu_cf, 1);
        tcg_gen_xori_i32(tmp2, cpu_zf, 1);
        tcg_gen_and_i32(taken, tmp1, tmp2);
        break;
    case 0x0E: /* LS: C or Z */
        tcg_gen_or_i32(taken, cpu_cf, cpu_zf);
        break;
    default:
        tcg_gen_movi_i32(taken, 0);
        break;
    }
    return taken;

}


static const TCGCond setcc_conds[8] = {TCG_COND_EQ, TCG_COND_NE, TCG_COND_LT, TCG_COND_GE,TCG_COND_LTU, TCG_COND_GEU, TCG_COND_LE, TCG_COND_GT,};

static bool trans_MOV(DisasContext *dc, arg_MOV *a)
{
    tcg_gen_mov_i32(cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_MOV_U6(DisasContext *dc, arg_MOV_U6 *a)
{
    tcg_gen_movi_i32(cpu_regs[a->b], a->u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_MOV_S12(DisasContext *dc, arg_MOV_S12 *a)
{
    tcg_gen_movi_i32(cpu_regs[a->b], a->s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_MOV_CC_F(DisasContext *dc, arg_MOV_CC_F *a)
{
    TCGv_i32 cond = gen_cc_test(a->q);
    if (a->f) {
        TCGv_i32 new_z = tcg_temp_new_i32();
        TCGv_i32 new_n = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, cpu_regs[a->c], 0);
        tcg_gen_shri_i32(new_n, cpu_regs[a->c], 31);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), cpu_regs[a->c], cpu_regs[a->b]);
    return true;
}

static bool trans_MOV_CC_F_U6(DisasContext *dc, arg_MOV_CC_F_U6 *a)
{
    TCGv_i32 cond = gen_cc_test(a->q);
    if (a->f) {
        TCGv_i32 new_z = tcg_temp_new_i32();
        TCGv_i32 new_n = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tcg_constant_i32(a->u), 0);
        tcg_gen_shri_i32(new_n, tcg_constant_i32(a->u), 31);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tcg_constant_i32(a->u), cpu_regs[a->b]);
    return true;
}

static bool trans_MOV_S_H_S3(DisasContext *dc, arg_MOV_S_H_S3 *a)
{
    tcg_gen_movi_i32(cpu_regs[a->h], a->s);
    return true;
}

static bool trans_MOV_S_NE(DisasContext *dc, arg_MOV_S_NE *a)
{
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[arc_reduced_regs[a->b]],cpu_zf, tcg_constant_i32(0),cpu_regs[a->h], cpu_regs[a->b]);
    return true;
}

static bool trans_MOV_S_U8(DisasContext *dc, arg_MOV_S_U8 *a)
{
    tcg_gen_movi_i32(cpu_regs[arc_reduced_regs[a->b]], a->u);
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

static bool trans_ADD(DisasContext *dc, arg_ADD *a)
{   
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_3 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_3 = tcg_temp_new_i32();
        TCGv_i32 vf_res_3 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_3, orig_b, cpu_regs[a->c]);
        tcg_gen_not_i32(vf_t0_3, vf_t0_3);
        tcg_gen_xor_i32(vf_t1_3, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_3, vf_t0_3, vf_t1_3);
        tcg_gen_shri_i32(vf_res_3, vf_t0_3, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_3);
    }
    return true;
}

static bool trans_ADD_U6(DisasContext *dc, arg_ADD_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);    
    tcg_gen_addi_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_4 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_4 = tcg_temp_new_i32();
        TCGv_i32 vf_res_4 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_4, orig_b, tcg_constant_i32(a->u));
        tcg_gen_not_i32(vf_t0_4, vf_t0_4);
        tcg_gen_xor_i32(vf_t1_4, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_4, vf_t0_4, vf_t1_4);
        tcg_gen_shri_i32(vf_res_4, vf_t0_4, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_4);
    }
    return true;
}

static bool trans_ADD_S12(DisasContext *dc, arg_ADD_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_addi_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_b);
        TCGv_i32 vf_t0_5 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_5 = tcg_temp_new_i32();
        TCGv_i32 vf_res_5 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_5, orig_b, tcg_constant_i32(a->s));
        tcg_gen_not_i32(vf_t0_5, vf_t0_5);
        tcg_gen_xor_i32(vf_t1_5, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_5, vf_t0_5, vf_t1_5);
        tcg_gen_shri_i32(vf_res_5, vf_t0_5, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_5);
    }
    return true;
}

static bool trans_ADD_CC_F(DisasContext *dc, arg_ADD_CC_F *a)
{
    TCGv_i32 cond = gen_cc_test(a->q);
    TCGv_i32 new_val = tcg_temp_new_i32();
    tcg_gen_add_i32(new_val, cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        TCGv_i32 new_z = tcg_temp_new_i32();
        TCGv_i32 new_n = tcg_temp_new_i32();
        TCGv_i32 new_c = tcg_temp_new_i32();
        TCGv_i32 new_v = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, new_val, 0);
        tcg_gen_shri_i32(new_n, new_val, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, new_c, new_val, cpu_regs[a->b]);
        TCGv_i32 vf_t0_6 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_6 = tcg_temp_new_i32();
        TCGv_i32 vf_res_6 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_6, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_not_i32(vf_t0_6, vf_t0_6);
        tcg_gen_xor_i32(vf_t1_6, cpu_regs[a->b], new_val);
        tcg_gen_and_i32(vf_t0_6, vf_t0_6, vf_t1_6);
        tcg_gen_shri_i32(vf_res_6, vf_t0_6, 31);
        new_v = vf_res_6;
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), new_val, cpu_regs[a->b]);
    return true;
}

static bool trans_ADD_CC_F_U6(DisasContext *dc, arg_ADD_CC_F_U6 *a)
{
    TCGv_i32 cond = gen_cc_test(a->q);
    TCGv_i32 new_val = tcg_temp_new_i32();
    tcg_gen_addi_i32(new_val, cpu_regs[a->b], a->u);
    if (a->f) {
        TCGv_i32 new_z = tcg_temp_new_i32();
        TCGv_i32 new_n = tcg_temp_new_i32();
        TCGv_i32 new_c = tcg_temp_new_i32();
        TCGv_i32 new_v = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, new_val, 0);
        tcg_gen_shri_i32(new_n, new_val, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, new_c, new_val, cpu_regs[a->b]);
        TCGv_i32 vf_t0_7 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_7 = tcg_temp_new_i32();
        TCGv_i32 vf_res_7 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_7, cpu_regs[a->b], tcg_constant_i32(a->u));
        tcg_gen_not_i32(vf_t0_7, vf_t0_7);
        tcg_gen_xor_i32(vf_t1_7, cpu_regs[a->b], new_val);
        tcg_gen_and_i32(vf_t0_7, vf_t0_7, vf_t1_7);
        tcg_gen_shri_i32(vf_res_7, vf_t0_7, 31);
        new_v = vf_res_7;
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), new_val, cpu_regs[a->b]);
    return true;
}

static bool trans_ADD1(DisasContext *dc, arg_ADD1 *a)
{   
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], shift_c);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_8 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_8 = tcg_temp_new_i32();
        TCGv_i32 vf_res_8 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_8, orig_b, shift_c);
        tcg_gen_not_i32(vf_t0_8, vf_t0_8);
        tcg_gen_xor_i32(vf_t1_8, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_8, vf_t0_8, vf_t1_8);
        tcg_gen_shri_i32(vf_res_8, vf_t0_8, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_8);
    }
    return true;
}

static bool trans_ADD1_U6(DisasContext *dc, arg_ADD1_U6 *a)
{
    TCGv_i32 shift_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_u, a->u << 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], shift_u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_9 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_9 = tcg_temp_new_i32();
        TCGv_i32 vf_res_9 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_9, orig_b, shift_u);
        tcg_gen_not_i32(vf_t0_9, vf_t0_9);
        tcg_gen_xor_i32(vf_t1_9, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_9, vf_t0_9, vf_t1_9);
        tcg_gen_shri_i32(vf_res_9, vf_t0_9, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_9);
    }
    return true;
}

static bool trans_ADD1_S12(DisasContext *dc, arg_ADD1_S12 *a)
{
    TCGv_i32 shift_s = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_s, a->s << 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->b], cpu_regs[a->b], shift_s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_b);
        TCGv_i32 vf_t0_10 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_10 = tcg_temp_new_i32();
        TCGv_i32 vf_res_10 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_10, orig_b, shift_s);
        tcg_gen_not_i32(vf_t0_10, vf_t0_10);
        tcg_gen_xor_i32(vf_t1_10, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_10, vf_t0_10, vf_t1_10);
        tcg_gen_shri_i32(vf_res_10, vf_t0_10, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_10);
    }
    return true;
}

static bool trans_ADD1_CC_F(DisasContext *dc, arg_ADD_CC_F *a)
{
    {
        TCGv_i32 shift_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 1);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], shift_c);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 vf_t0_11 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_11 = tcg_temp_new_i32();
            TCGv_i32 vf_res_11 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_11, cpu_regs[a->b], shift_c);
            tcg_gen_not_i32(vf_t0_11, vf_t0_11);
            tcg_gen_xor_i32(vf_t1_11, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_11, vf_t0_11, vf_t1_11);
            tcg_gen_shri_i32(vf_res_11, vf_t0_11, 31);
            new_v = vf_res_11;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ADD1_CC_F_U6(DisasContext *dc, arg_ADD1_CC_F_U6 *a)
{
    {
        TCGv_i32 shift_u = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_u, a->u << 1);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], shift_u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 vf_t0_12 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_12 = tcg_temp_new_i32();
            TCGv_i32 vf_res_12 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_12, cpu_regs[a->b], shift_u);
            tcg_gen_not_i32(vf_t0_12, vf_t0_12);
            tcg_gen_xor_i32(vf_t1_12, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_12, vf_t0_12, vf_t1_12);
            tcg_gen_shri_i32(vf_res_12, vf_t0_12, 31);
            new_v = vf_res_12;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ADD2(DisasContext *dc, arg_ADD2 *a)
{   
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], shift_c);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_13 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_13 = tcg_temp_new_i32();
        TCGv_i32 vf_res_13 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_13, orig_b, shift_c);
        tcg_gen_not_i32(vf_t0_13, vf_t0_13);
        tcg_gen_xor_i32(vf_t1_13, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_13, vf_t0_13, vf_t1_13);
        tcg_gen_shri_i32(vf_res_13, vf_t0_13, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_13);
    }
    return true;
}

static bool trans_ADD2_U6(DisasContext *dc, arg_ADD2_U6 *a)
{
    TCGv_i32 shift_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_u, a->u << 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], shift_u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_14 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_14 = tcg_temp_new_i32();
        TCGv_i32 vf_res_14 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_14, orig_b, shift_u);
        tcg_gen_not_i32(vf_t0_14, vf_t0_14);
        tcg_gen_xor_i32(vf_t1_14, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_14, vf_t0_14, vf_t1_14);
        tcg_gen_shri_i32(vf_res_14, vf_t0_14, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_14);
    }
    return true;
}

static bool trans_ADD2_S12(DisasContext *dc, arg_ADD2_S12 *a)
{
    TCGv_i32 shift_s = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_s, a->s << 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->b], cpu_regs[a->b], shift_s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_b);
        TCGv_i32 vf_t0_15 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_15 = tcg_temp_new_i32();
        TCGv_i32 vf_res_15 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_15, orig_b, shift_s);
        tcg_gen_not_i32(vf_t0_15, vf_t0_15);
        tcg_gen_xor_i32(vf_t1_15, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_15, vf_t0_15, vf_t1_15);
        tcg_gen_shri_i32(vf_res_15, vf_t0_15, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_15);
    }
    return true;
}

static bool trans_ADD2_CC_F(DisasContext *dc, arg_ADD2_CC_F *a)
{
    {
        TCGv_i32 shift_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 2);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], shift_c);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 vf_t0_16 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_16 = tcg_temp_new_i32();
            TCGv_i32 vf_res_16 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_16, cpu_regs[a->b], shift_c);
            tcg_gen_not_i32(vf_t0_16, vf_t0_16);
            tcg_gen_xor_i32(vf_t1_16, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_16, vf_t0_16, vf_t1_16);
            tcg_gen_shri_i32(vf_res_16, vf_t0_16, 31);
            new_v = vf_res_16;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ADD2_CC_F_U6(DisasContext *dc, arg_ADD2_CC_F_U6 *a)
{
    {
        TCGv_i32 shift_u = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_u, a->u << 2);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], shift_u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 vf_t0_17 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_17 = tcg_temp_new_i32();
            TCGv_i32 vf_res_17 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_17, cpu_regs[a->b], shift_u);
            tcg_gen_not_i32(vf_t0_17, vf_t0_17);
            tcg_gen_xor_i32(vf_t1_17, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_17, vf_t0_17, vf_t1_17);
            tcg_gen_shri_i32(vf_res_17, vf_t0_17, 31);
            new_v = vf_res_17;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ADD3(DisasContext *dc, arg_ADD3 *a)
{   
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], shift_c);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_18 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_18 = tcg_temp_new_i32();
        TCGv_i32 vf_res_18 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_18, orig_b, shift_c);
        tcg_gen_not_i32(vf_t0_18, vf_t0_18);
        tcg_gen_xor_i32(vf_t1_18, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_18, vf_t0_18, vf_t1_18);
        tcg_gen_shri_i32(vf_res_18, vf_t0_18, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_18);
    }
    return true;
}

static bool trans_ADD3_U6(DisasContext *dc, arg_ADD3_U6 *a)
{
    TCGv_i32 shift_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_u, a->u << 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->a], cpu_regs[a->b], shift_u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 vf_t0_19 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_19 = tcg_temp_new_i32();
        TCGv_i32 vf_res_19 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_19, orig_b, shift_u);
        tcg_gen_not_i32(vf_t0_19, vf_t0_19);
        tcg_gen_xor_i32(vf_t1_19, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_19, vf_t0_19, vf_t1_19);
        tcg_gen_shri_i32(vf_res_19, vf_t0_19, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_19);
    }
    return true;
}

static bool trans_ADD3_S12(DisasContext *dc, arg_ADD3_S12 *a)
{
    TCGv_i32 shift_s = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_s, a->s << 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_add_i32(cpu_regs[a->b], cpu_regs[a->b], shift_s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_b);
        TCGv_i32 vf_t0_20 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_20 = tcg_temp_new_i32();
        TCGv_i32 vf_res_20 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_20, orig_b, shift_s);
        tcg_gen_not_i32(vf_t0_20, vf_t0_20);
        tcg_gen_xor_i32(vf_t1_20, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_20, vf_t0_20, vf_t1_20);
        tcg_gen_shri_i32(vf_res_20, vf_t0_20, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_20);
    }
    return true;
}

static bool trans_ADD3_CC_F(DisasContext *dc, arg_ADD3_CC_F *a)
{
    {
        TCGv_i32 shift_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 3);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], shift_c);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 vf_t0_21 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_21 = tcg_temp_new_i32();
            TCGv_i32 vf_res_21 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_21, cpu_regs[a->b], shift_c);
            tcg_gen_not_i32(vf_t0_21, vf_t0_21);
            tcg_gen_xor_i32(vf_t1_21, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_21, vf_t0_21, vf_t1_21);
            tcg_gen_shri_i32(vf_res_21, vf_t0_21, 31);
            new_v = vf_res_21;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ADD3_CC_F_U6(DisasContext *dc, arg_ADD3_CC_F_U6 *a)
{
    {
        TCGv_i32 shift_u = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_u, a->u << 3);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], shift_u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 vf_t0_22 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_22 = tcg_temp_new_i32();
            TCGv_i32 vf_res_22 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_22, cpu_regs[a->b], shift_u);
            tcg_gen_not_i32(vf_t0_22, vf_t0_22);
            tcg_gen_xor_i32(vf_t1_22, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_22, vf_t0_22, vf_t1_22);
            tcg_gen_shri_i32(vf_res_22, vf_t0_22, 31);
            new_v = vf_res_22;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ADD_S(DisasContext *dc, arg_ADD_S *a)
{
    tcg_gen_add_i32(cpu_regs[arc_reduced_regs[a->a]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ADD_S_H(DisasContext *dc, arg_ADD_S_H *a)
{
    tcg_gen_add_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->h]);
    return true;
}

static bool trans_ADD_S_S3(DisasContext *dc, arg_ADD_S_S3 *a)
{
    tcg_gen_addi_i32(cpu_regs[a->h], cpu_regs[a->h], a->s);
    return true;
}

static bool trans_ADD_S_U7(DisasContext *dc, arg_ADD_S_U7 *a)
{
    tcg_gen_addi_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ADD_S_U3(DisasContext *dc, arg_ADD_S_U3 *a)
{
    tcg_gen_addi_i32(cpu_regs[arc_reduced_regs[a->c]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ADD_S_SP_U7(DisasContext *dc, arg_ADD_S_SP_U7 *a)
{
    tcg_gen_addi_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[SP], a->u << 2);
    return true;
}

static bool trans_ADD_S_SP_SP_U7(DisasContext *dc, arg_ADD_S_SP_SP_U7 *a)
{
    tcg_gen_addi_i32(cpu_regs[SP], cpu_regs[SP], a->u << 2);
    return true;
}

static bool trans_ADD1_S(DisasContext *dc, arg_ADD1_S *a)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[arc_reduced_regs[a->c]], 1);
    tcg_gen_add_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], shift_c);
    return true;
}

static bool trans_ADD2_S(DisasContext *dc, arg_ADD2_S *a)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[arc_reduced_regs[a->c]], 2);
    tcg_gen_add_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], shift_c);
    return true;
}

static bool trans_ADD3_S(DisasContext *dc, arg_ADD3_S *a)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[arc_reduced_regs[a->c]], 3);
    tcg_gen_add_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], shift_c);
    return true;
}

static bool trans_MPY(DisasContext *dc, arg_MPY *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->a], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo, 0);
        tcg_gen_shri_i32(cpu_nf, hi, 31);
        {
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
        }
    }
    return true;
}

static bool trans_MPY_u6(DisasContext *dc, arg_MPY_u6 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_mov_i32(cpu_regs[a->a], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo, 0);
        tcg_gen_shri_i32(cpu_nf, hi, 31);
        {
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
        }
    }
    return true;
}

static bool trans_MPY_s12(DisasContext *dc, arg_MPY_s12 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->s));
    tcg_gen_mov_i32(cpu_regs[a->b], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo, 0);
        tcg_gen_shri_i32(cpu_nf, hi, 31);
        {
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
        }
    }
    return true;
}

static bool trans_MPY_CC_F(DisasContext *dc, arg_MPY_CC_F *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, lo, 0);
            tcg_gen_shri_i32(new_n, hi, 31);
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, new_v, hi, sign_ext);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), lo, cpu_regs[a->b]);
      }
    return true;
}

static bool trans_MPY_CC_F_U6(DisasContext *dc, arg_MPY_CC_F_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, lo, 0);
            tcg_gen_shri_i32(new_n, hi, 31);
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, new_v, hi, sign_ext);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), lo, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_MPY_S(DisasContext *dc, arg_MPY_S *a)
{
    tcg_gen_mul_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}
static bool trans_SUB(DisasContext *dc, arg_SUB *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, cpu_regs[a->c]);
        TCGv_i32 vf_t0_23 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_23 = tcg_temp_new_i32();
        TCGv_i32 vf_res_23 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_23, orig_b, cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_23, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_23, vf_t0_23, vf_t1_23);
        tcg_gen_shri_i32(vf_res_23, vf_t0_23, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_23);
    }
    return true;
}

static bool trans_SUB_u6(DisasContext *dc, arg_SUB_u6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_subi_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, tcg_constant_i32(a->u));
        TCGv_i32 vf_t0_24 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_24 = tcg_temp_new_i32();
        TCGv_i32 vf_res_24 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_24, orig_b, tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_24, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_24, vf_t0_24, vf_t1_24);
        tcg_gen_shri_i32(vf_res_24, vf_t0_24, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_24);
    }
    return true;
}

static bool trans_SUB_s12(DisasContext *dc, arg_SUB_s12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_subi_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, tcg_constant_i32(a->s));
        TCGv_i32 vf_t0_25 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_25 = tcg_temp_new_i32();
        TCGv_i32 vf_res_25 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_25, orig_b, tcg_constant_i32(a->s));
        tcg_gen_xor_i32(vf_t1_25, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_25, vf_t0_25, vf_t1_25);
        tcg_gen_shri_i32(vf_res_25, vf_t0_25, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_25);
    }
    return true;
}

static bool trans_SUB_CC(DisasContext *dc, arg_SUB_CC *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], cpu_regs[a->c]);
            TCGv_i32 vf_t0_26 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_26 = tcg_temp_new_i32();
            TCGv_i32 vf_res_26 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_26, cpu_regs[a->b], cpu_regs[a->c]);
            tcg_gen_xor_i32(vf_t1_26, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_26, vf_t0_26, vf_t1_26);
            tcg_gen_shri_i32(vf_res_26, vf_t0_26, 31);
            new_v = vf_res_26;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
  }


static bool trans_SUB_CC_U6(DisasContext *dc, arg_SUB_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_subi_i32(tmp, cpu_regs[a->b], a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcondi_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], a->u);
            TCGv_i32 vf_t0_27 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_27 = tcg_temp_new_i32();
            TCGv_i32 vf_res_27 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_27, cpu_regs[a->b], tcg_constant_i32(a->u));
            tcg_gen_xor_i32(vf_t1_27, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_27, vf_t0_27, vf_t1_27);
            tcg_gen_shri_i32(vf_res_27, vf_t0_27, 31);
            new_v = vf_res_27;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_SUB1(DisasContext *dc, arg_SUB1 *a)
{   
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], shift_c);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_c);
        TCGv_i32 vf_t0_28 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_28 = tcg_temp_new_i32();
        TCGv_i32 vf_res_28 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_28, orig_b, shift_c);
        tcg_gen_xor_i32(vf_t1_28, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_28, vf_t0_28, vf_t1_28);
        tcg_gen_shri_i32(vf_res_28, vf_t0_28, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_28);
    }
    return true;
}

static bool trans_SUB1_U6(DisasContext *dc, arg_SUB1_U6 *a)
{
    TCGv_i32 shift_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_u, a->u << 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], shift_u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_u);
        TCGv_i32 vf_t0_29 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_29 = tcg_temp_new_i32();
        TCGv_i32 vf_res_29 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_29, orig_b, shift_u);
        tcg_gen_xor_i32(vf_t1_29, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_29, vf_t0_29, vf_t1_29);
        tcg_gen_shri_i32(vf_res_29, vf_t0_29, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_29);
    }
    return true;
}

static bool trans_SUB1_S12(DisasContext *dc, arg_SUB1_S12 *a)
{
    TCGv_i32 shift_s = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_s, a->s << 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->b], cpu_regs[a->b], shift_s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_s);
        TCGv_i32 vf_t0_30 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_30 = tcg_temp_new_i32();
        TCGv_i32 vf_res_30 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_30, orig_b, shift_s);
        tcg_gen_xor_i32(vf_t1_30, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_30, vf_t0_30, vf_t1_30);
        tcg_gen_shri_i32(vf_res_30, vf_t0_30, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_30);
    }
    return true;
}

static bool trans_SUB1_CC(DisasContext *dc, arg_SUB1_CC *a)
{
    {
        TCGv_i32 shift_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 1);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], shift_c);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], shift_c);
            TCGv_i32 vf_t0_31 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_31 = tcg_temp_new_i32();
            TCGv_i32 vf_res_31 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_31, cpu_regs[a->b], shift_c);
            tcg_gen_xor_i32(vf_t1_31, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_31, vf_t0_31, vf_t1_31);
            tcg_gen_shri_i32(vf_res_31, vf_t0_31, 31);
            new_v = vf_res_31;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_SUB1_CC_U6(DisasContext *dc, arg_SUB1_CC_U6 *a)
{
    {
        TCGv_i32 shift_u = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_u, a->u << 1);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], shift_u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], shift_u);
            TCGv_i32 vf_t0_32 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_32 = tcg_temp_new_i32();
            TCGv_i32 vf_res_32 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_32, cpu_regs[a->b], shift_u);
            tcg_gen_xor_i32(vf_t1_32, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_32, vf_t0_32, vf_t1_32);
            tcg_gen_shri_i32(vf_res_32, vf_t0_32, 31);
            new_v = vf_res_32;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_SUB2(DisasContext *dc, arg_SUB2 *a)
{   
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], shift_c);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_c);
        TCGv_i32 vf_t0_33 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_33 = tcg_temp_new_i32();
        TCGv_i32 vf_res_33 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_33, orig_b, shift_c);
        tcg_gen_xor_i32(vf_t1_33, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_33, vf_t0_33, vf_t1_33);
        tcg_gen_shri_i32(vf_res_33, vf_t0_33, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_33);
    }
    return true;
}

static bool trans_SUB2_U6(DisasContext *dc, arg_SUB2_U6 *a)
{
    TCGv_i32 shift_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_u, a->u << 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], shift_u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_u);
        TCGv_i32 vf_t0_34 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_34 = tcg_temp_new_i32();
        TCGv_i32 vf_res_34 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_34, orig_b, shift_u);
        tcg_gen_xor_i32(vf_t1_34, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_34, vf_t0_34, vf_t1_34);
        tcg_gen_shri_i32(vf_res_34, vf_t0_34, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_34);
    }
    return true;
}

static bool trans_SUB2_S12(DisasContext *dc, arg_SUB2_S12 *a)
{
    TCGv_i32 shift_s = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_s, a->s << 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->b], cpu_regs[a->b], shift_s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_s);
        TCGv_i32 vf_t0_35 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_35 = tcg_temp_new_i32();
        TCGv_i32 vf_res_35 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_35, orig_b, shift_s);
        tcg_gen_xor_i32(vf_t1_35, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_35, vf_t0_35, vf_t1_35);
        tcg_gen_shri_i32(vf_res_35, vf_t0_35, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_35);
    }
    return true;
}

static bool trans_SUB2_CC(DisasContext *dc, arg_SUB2_CC *a)
{
    {
        TCGv_i32 shift_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 2);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], shift_c);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], shift_c);
            TCGv_i32 vf_t0_36 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_36 = tcg_temp_new_i32();
            TCGv_i32 vf_res_36 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_36, cpu_regs[a->b], shift_c);
            tcg_gen_xor_i32(vf_t1_36, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_36, vf_t0_36, vf_t1_36);
            tcg_gen_shri_i32(vf_res_36, vf_t0_36, 31);
            new_v = vf_res_36;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_SUB2_CC_U6(DisasContext *dc, arg_SUB2_CC_U6 *a)
{
    {
        TCGv_i32 shift_u = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_u, a->u << 2);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], shift_u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], shift_u);
            TCGv_i32 vf_t0_37 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_37 = tcg_temp_new_i32();
            TCGv_i32 vf_res_37 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_37, cpu_regs[a->b], shift_u);
            tcg_gen_xor_i32(vf_t1_37, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_37, vf_t0_37, vf_t1_37);
            tcg_gen_shri_i32(vf_res_37, vf_t0_37, 31);
            new_v = vf_res_37;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_SUB3(DisasContext *dc, arg_SUB3 *a)
{   
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], shift_c);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_c);
        TCGv_i32 vf_t0_38 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_38 = tcg_temp_new_i32();
        TCGv_i32 vf_res_38 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_38, orig_b, shift_c);
        tcg_gen_xor_i32(vf_t1_38, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_38, vf_t0_38, vf_t1_38);
        tcg_gen_shri_i32(vf_res_38, vf_t0_38, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_38);
    }
    return true;
}

static bool trans_SUB3_U6(DisasContext *dc, arg_SUB3_U6 *a)
{
    TCGv_i32 shift_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_u, a->u << 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->b], shift_u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_u);
        TCGv_i32 vf_t0_39 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_39 = tcg_temp_new_i32();
        TCGv_i32 vf_res_39 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_39, orig_b, shift_u);
        tcg_gen_xor_i32(vf_t1_39, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_39, vf_t0_39, vf_t1_39);
        tcg_gen_shri_i32(vf_res_39, vf_t0_39, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_39);
    }
    return true;
}

static bool trans_SUB3_S12(DisasContext *dc, arg_SUB3_S12 *a)
{
    TCGv_i32 shift_s = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_s, a->s << 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->b], cpu_regs[a->b], shift_s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_s);
        TCGv_i32 vf_t0_40 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_40 = tcg_temp_new_i32();
        TCGv_i32 vf_res_40 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_40, orig_b, shift_s);
        tcg_gen_xor_i32(vf_t1_40, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_40, vf_t0_40, vf_t1_40);
        tcg_gen_shri_i32(vf_res_40, vf_t0_40, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_40);
    }
    return true;
}

static bool trans_SUB3_CC(DisasContext *dc, arg_SUB3_CC *a)
{
    {
        TCGv_i32 shift_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(shift_c, cpu_regs[a->c], 3);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], shift_c);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], shift_c);
            TCGv_i32 vf_t0_41 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_41 = tcg_temp_new_i32();
            TCGv_i32 vf_res_41 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_41, cpu_regs[a->b], shift_c);
            tcg_gen_xor_i32(vf_t1_41, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_41, vf_t0_41, vf_t1_41);
            tcg_gen_shri_i32(vf_res_41, vf_t0_41, 31);
            new_v = vf_res_41;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_SUB3_CC_U6(DisasContext *dc, arg_SUB3_CC_U6 *a)
{
    {
        TCGv_i32 shift_u = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_u, a->u << 3);
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], shift_u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, cpu_regs[a->b], shift_u);
            TCGv_i32 vf_t0_42 = tcg_temp_new_i32();
            TCGv_i32 vf_t1_42 = tcg_temp_new_i32();
            TCGv_i32 vf_res_42 = tcg_temp_new_i32();
            tcg_gen_xor_i32(vf_t0_42, cpu_regs[a->b], shift_u);
            tcg_gen_xor_i32(vf_t1_42, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(vf_t0_42, vf_t0_42, vf_t1_42);
            tcg_gen_shri_i32(vf_res_42, vf_t0_42, 31);
            new_v = vf_res_42;
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_SUB_S_U3(DisasContext *dc, arg_SUB_S_U3 *a)
{
    tcg_gen_subi_i32(cpu_regs[arc_reduced_regs[a->c]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_SUB_S_NE(DisasContext *dc, arg_SUB_S_NE *a)
{
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[arc_reduced_regs[a->b]], cpu_zf, tcg_constant_i32(0), tcg_constant_i32(0), cpu_regs[arc_reduced_regs[a->b]]);
    return true;
}

static bool trans_SUB_S_C(DisasContext *dc, arg_SUB_S_C *a)
{
    tcg_gen_sub_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_SUB_S_U5(DisasContext *dc, arg_SUB_S_U5 *a)
{
    tcg_gen_subi_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_SUB_S_SP_U7(DisasContext *dc, arg_SUB_S_SP_U7 *a)
{
    tcg_gen_subi_i32(cpu_regs[SP], cpu_regs[SP], a->u << 2);
    return true;
}

static bool trans_AND(DisasContext *dc, arg_AND *a)
{
    tcg_gen_and_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_AND_U6(DisasContext *dc, arg_AND_U6 *a)
{
    tcg_gen_andi_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_AND_S12(DisasContext *dc, arg_AND_S12 *a)
{
    tcg_gen_andi_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_AND_CC(DisasContext *dc, arg_AND_CC *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_and_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_AND_CC_U6(DisasContext *dc, arg_AND_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_andi_i32(tmp, cpu_regs[a->b], a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_AND_S(DisasContext *dc, arg_AND_S *a)
{
    tcg_gen_and_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_OR(DisasContext *dc, arg_OR *a)
{
    tcg_gen_or_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_OR_U6(DisasContext *dc, arg_OR_U6 *a)
{
    tcg_gen_ori_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_OR_S12(DisasContext *dc, arg_OR_S12 *a)
{
    tcg_gen_ori_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_OR_CC(DisasContext *dc, arg_OR_CC *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_or_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_OR_CC_U6(DisasContext *dc, arg_OR_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_ori_i32(tmp, cpu_regs[a->b], a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_OR_S(DisasContext *dc, arg_OR_S *a)
{
    tcg_gen_or_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_XOR(DisasContext *dc, arg_XOR *a)
{
    tcg_gen_xor_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_XOR_U6(DisasContext *dc, arg_XOR_U6 *a)
{
    tcg_gen_xori_i32(cpu_regs[a->a], cpu_regs[a->b], a->u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_XOR_S12(DisasContext *dc, arg_XOR_S12 *a)
{
    tcg_gen_xori_i32(cpu_regs[a->b], cpu_regs[a->b], a->s);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_XOR_CC(DisasContext *dc, arg_XOR_CC *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_xor_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_XOR_CC_U6(DisasContext *dc, arg_XOR_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_xori_i32(tmp, cpu_regs[a->b],a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_XOR_S(DisasContext *dc, arg_XOR_S *a)
{
    tcg_gen_xor_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ASL(DisasContext *dc, arg_ASL *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_shli_i32(cpu_regs[a->b], cpu_regs[a->c], 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
    return true;
}

static bool trans_ASL_U6(DisasContext *dc, arg_ASL_U6 *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_movi_i32(orig_c, a->u);
    tcg_gen_shli_i32(cpu_regs[a->b], orig_c , 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
    return true;
}

static bool trans_ASL_F(DisasContext *dc, arg_ASL_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ASL_U6_F(DisasContext *dc, arg_ASL_U6_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->u & 31);
    tcg_gen_shl_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ASL_S12(DisasContext *dc, arg_ASL_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->s & 31);
    tcg_gen_shl_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ASL_CC(DisasContext *dc, arg_ASL_CC *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_shl_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_shr_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ASL_CC_U6(DisasContext *dc, arg_ASL_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_shl_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_shr_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ASL_S(DisasContext *dc, arg_ASL_S *a)
{
    tcg_gen_shl_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ASL_S_F(DisasContext *dc, arg_ASL_S_F *a)
{
    tcg_gen_shl_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ASL_S_U3(DisasContext *dc, arg_ASL_S_U3 *a)
{
    tcg_gen_shli_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ASL_S_U5(DisasContext *dc, arg_ASL_S_U5 *a)
{
    tcg_gen_shli_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_LSR(DisasContext *dc, arg_LSR *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_shri_i32(cpu_regs[a->b], cpu_regs[a->c], 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_andi_i32(cpu_cf, orig_c, 1);
    }
    return true;
}

static bool trans_LSR_U6(DisasContext *dc, arg_LSR_U6 *a)
{
    TCGv_i32 orig_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(orig_u, a->u);
    tcg_gen_shri_i32(cpu_regs[a->b], orig_u, 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_andi_i32(cpu_cf, orig_u, 1);
    }
    return true;
}

static bool trans_LSR_F(DisasContext *dc, arg_LSR_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
    tcg_gen_shr_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_LSR_U6_F(DisasContext *dc, arg_LSR_U6_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->u & 31);
    tcg_gen_shr_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_LSR_S12(DisasContext *dc, arg_LSR_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->s & 31);
    tcg_gen_shr_i32(cpu_regs[a->b], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_LSR_CC(DisasContext *dc, arg_LSR_CC *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_shr_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subi_i32(inv_amt, shift_amt, 1);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_shr_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_LSR_CC_U6(DisasContext *dc, arg_LSR_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_shr_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subi_i32(inv_amt, shift_amt, 1);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_shr_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_LSR_S(DisasContext *dc, arg_LSR_S *a)
{
    tcg_gen_shr_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_LSR_S_F(DisasContext *dc, arg_LSR_S_F *a)
{
    tcg_gen_shr_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_LSR_S_U5(DisasContext *dc, arg_LSR_S_U5 *a)
{
    tcg_gen_shri_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ASR(DisasContext *dc, arg_ASR *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_sari_i32(cpu_regs[a->b], cpu_regs[a->c], 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_andi_i32(cpu_cf, orig_c, 1);
    }
    return true;
}

static bool trans_ASR_U6(DisasContext *dc, arg_ASR_U6 *a)
{
    TCGv_i32 orig_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(orig_u, a->u);
    tcg_gen_sari_i32(cpu_regs[a->b], orig_u, 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_andi_i32(cpu_cf, orig_u, 1);
    } 
    return true;
}

static bool trans_ASR_F(DisasContext *dc, arg_ASR_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
    tcg_gen_sar_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, pos);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ASR_U6_F(DisasContext *dc, arg_ASR_U6_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->u & 31);
    tcg_gen_sar_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, pos);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ASR_S12(DisasContext *dc, arg_ASR_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->s & 31);
    tcg_gen_sar_i32(cpu_regs[a->b], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, pos);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ASR_S_U3(DisasContext *dc, arg_ASR_S_U3 *a)
{
    tcg_gen_sari_i32(cpu_regs[arc_reduced_regs[a->c]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ASR_CC(DisasContext *dc, arg_ASR_CC *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sar_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subi_i32(inv_amt, shift_amt, 1);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_sar_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ASR_CC_U6(DisasContext *dc, arg_ASR_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sar_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subi_i32(inv_amt, shift_amt, 1);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_sar_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ASR_S(DisasContext *dc, arg_ASR_S *a)
{
    tcg_gen_sar_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ASR_S_F(DisasContext *dc, arg_ASR_S_F *a)
{
    tcg_gen_sar_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ASR_S_U5(DisasContext *dc, arg_ASR_S_U5 *a)
{
    tcg_gen_sari_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ROR(DisasContext *dc, arg_ROR *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_rotri_i32(cpu_regs[a->b], cpu_regs[a->c], 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_andi_i32(cpu_cf, orig_c, 1);
    }
    return true;
}

static bool trans_ROR_U6(DisasContext *dc, arg_ROR_U6 *a)
{
    TCGv_i32 orig_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(orig_u, a->u);
    tcg_gen_rotri_i32(cpu_regs[a->b], orig_u, 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_andi_i32(cpu_cf, orig_u, 1);
    }
    return true;
}

static bool trans_ROR_F(DisasContext *dc, arg_ROR_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
    tcg_gen_rotr_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ROR_U6_F(DisasContext *dc, arg_ROR_U6_F *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->u & 31);
    tcg_gen_rotr_i32(cpu_regs[a->a], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_ROR_S12(DisasContext *dc, arg_ROR_S12 *a)
{
        TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_movi_i32(shift_amt, a->s & 31);
    tcg_gen_rotr_i32(cpu_regs[a->b], cpu_regs[a->b], shift_amt);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}


static bool trans_ROR_CC(DisasContext *dc, arg_ROR_CC *a)
{
{
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_rotr_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subi_i32(inv_amt, shift_amt, 1);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_sar_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ROR_CC_U6(DisasContext *dc, arg_ROR_CC_U6 *a)
{
    {
        TCGv_i32 cond = gen_cc_test(a->q);
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_rotr_i32(tmp, cpu_regs[a->b], shift_amt);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subi_i32(inv_amt, shift_amt, 1);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_sar_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, new_c, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_CMP(DisasContext *dc, arg_CMP *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], cpu_regs[a->c]);
    TCGv_i32 vf_t0_43 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_43 = tcg_temp_new_i32();
    TCGv_i32 vf_res_43 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_43, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_xor_i32(vf_t1_43, cpu_regs[a->b], tmp);
    tcg_gen_and_i32(vf_t0_43, vf_t0_43, vf_t1_43);
    tcg_gen_shri_i32(vf_res_43, vf_t0_43, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_43);
    return true;
}

static bool trans_CMP_U6(DisasContext *dc, arg_CMP_U6 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], tcg_constant_i32(a->u));
    TCGv_i32 vf_t0_44 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_44 = tcg_temp_new_i32();
    TCGv_i32 vf_res_44 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_44, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_xor_i32(vf_t1_44, cpu_regs[a->b], tmp);
    tcg_gen_and_i32(vf_t0_44, vf_t0_44, vf_t1_44);
    tcg_gen_shri_i32(vf_res_44, vf_t0_44, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_44);
    return true;
}

static bool trans_CMP_S12(DisasContext *dc, arg_CMP_S12 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->s));
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], tcg_constant_i32(a->s));
    TCGv_i32 vf_t0_45 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_45 = tcg_temp_new_i32();
    TCGv_i32 vf_res_45 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_45, cpu_regs[a->b], tcg_constant_i32(a->s));
    tcg_gen_xor_i32(vf_t1_45, cpu_regs[a->b], tmp);
    tcg_gen_and_i32(vf_t0_45, vf_t0_45, vf_t1_45);
    tcg_gen_shri_i32(vf_res_45, vf_t0_45, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_45);
    return true;
}

static bool trans_CMP_CC(DisasContext *dc, arg_CMP_CC *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], cpu_regs[a->c]);
    TCGv_i32 vf_t0_46 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_46 = tcg_temp_new_i32();
    TCGv_i32 vf_res_46 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_46, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_xor_i32(vf_t1_46, cpu_regs[a->b], tmp);
    tcg_gen_and_i32(vf_t0_46, vf_t0_46, vf_t1_46);
    tcg_gen_shri_i32(vf_res_46, vf_t0_46, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_46);
    return true;
}

static bool trans_CMP_CC_U6(DisasContext *dc, arg_CMP_CC_U6 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], tcg_constant_i32(a->u));
    TCGv_i32 vf_t0_47 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_47 = tcg_temp_new_i32();
    TCGv_i32 vf_res_47 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_47, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_xor_i32(vf_t1_47, cpu_regs[a->b], tmp);
    tcg_gen_and_i32(vf_t0_47, vf_t0_47, vf_t1_47);
    tcg_gen_shri_i32(vf_res_47, vf_t0_47, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_47);
    return true;
}

static bool trans_CMP_S(DisasContext *dc, arg_CMP_S *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->h]]);
    return true;
}

static bool trans_CMP_S3(DisasContext *dc, arg_CMP_S3 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[arc_reduced_regs[a->h]], tcg_constant_i32(a->s));
    return true;
}

static bool trans_CMP_S_U7(DisasContext *dc, arg_cmp_s_u7 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[arc_reduced_regs[a->b]], tcg_constant_i32(a->u7));
    return true;
}

static bool trans_BRANCH(DisasContext *dc, arg_BRANCH *a)
{
    uint32_t target = dc->base.pc_next + (a->sb << 1);
    if (a->n) {
        dc->has_delay_slot = true;
        dc->delay_target = tcg_constant_i32(target);
    } else {
        tcg_gen_movi_i32(cpu_pc, target);
        dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool trans_BRANCH_S(DisasContext *dc, arg_BRANCH_S *a)
{
    uint32_t target = dc->base.pc_next + (a->sb << 1);
    tcg_gen_movi_i32(cpu_pc, target);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_BRANCH_C(DisasContext *dc, arg_BRANCH_C *a)
{
    uint32_t target = dc->base.pc_next + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, cpu_regs[a->b], bitpos);
    tcg_gen_andi_i32(bit, bit, 1);
    TCGv_i32 pc_dest = cpu_pc;
    if (a->n) {
        dc->has_delay_slot = true;
        dc->delay_target = tcg_temp_new_i32();
        pc_dest = dc->delay_target;
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, pc_dest, tcg_constant_i32(0), bit, tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    if (!a->n) {
      dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool trans_BRANCH_C_U6(DisasContext *dc, arg_BRANCH_C_U6 *a)
{
    uint32_t target = dc->base.pc_next + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, cpu_regs[a->b], bitpos);
    tcg_gen_andi_i32(bit, bit, 1);
    TCGv_i32 pc_dest = cpu_pc;
    if (a->n) {
        dc->has_delay_slot = true;
        dc->delay_target = tcg_temp_new_i32();
        pc_dest = dc->delay_target;
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, pc_dest, tcg_constant_i32(0), bit, tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    if (!a->n) {
      dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool trans_BRANCH1_C(DisasContext *dc, arg_BRANCH1_C *a)
{
    uint32_t target = dc->base.pc_next + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, cpu_regs[a->b], bitpos);
    tcg_gen_andi_i32(bit, bit, 1);
    TCGv_i32 pc_dest = cpu_pc;
    if (a->n) {
        dc->has_delay_slot = true;
        dc->delay_target = tcg_temp_new_i32();
        pc_dest = dc->delay_target;
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, pc_dest, tcg_constant_i32(1), bit, tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    if (!a->n) {
      dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool trans_BRANCH1_C_U6(DisasContext *dc, arg_BRANCH1_C_U6 *a)
{
    uint32_t target = dc->base.pc_next + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, cpu_regs[a->b], bitpos);
    tcg_gen_andi_i32(bit, bit, 1);
    TCGv_i32 pc_dest = cpu_pc;
    if (a->n) {
        dc->has_delay_slot = true;
        dc->delay_target = tcg_temp_new_i32();
        pc_dest = dc->delay_target;
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, pc_dest, tcg_constant_i32(1), bit, tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    if (!a->n) {
      dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool trans_BRANCH_CC(DisasContext *dc, arg_BRANCH_CC *a)
{
    TCGv_i32 cond = gen_cc_test(a->q);
    uint32_t target = dc->base.pc_next + (a->sb << 1);
    uint32_t fallthrough = dc->base.pc_next + 4;
    TCGv_i32 pc_dest = cpu_pc;
    if (a->n) {
        dc->has_delay_slot = true;
        dc->delay_target = tcg_temp_new_i32();
        pc_dest = dc->delay_target;
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, pc_dest, cond, tcg_constant_i32(1), tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    if (!a->n) {
        dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static bool trans_JCC(DisasContext *dc, arg_JCC *a)
{
    tcg_gen_mov_i32(cpu_pc, cpu_regs[a->c]);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_JCC_U6(DisasContext *dc, arg_JCC_U6 *a)
{
    tcg_gen_movi_i32(cpu_pc, a->u);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_JCCD(DisasContext *dc, arg_JCCD *a)
{
    dc->has_delay_slot = true;
    dc->delay_target = tcg_temp_new_i32();
    tcg_gen_mov_i32(dc->delay_target, cpu_regs[a->c]);
    return true;
}

static bool trans_JCCD_U6(DisasContext *dc, arg_JCCD_U6 *a)
{
    dc->has_delay_slot = true;
    dc->delay_target = tcg_temp_new_i32();
    tcg_gen_movi_i32(dc->delay_target, a->u);
    return true;
}

static bool trans_NOT(DisasContext *dc, arg_NOT *a)
{
    tcg_gen_not_i32(cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_NOT_U6(DisasContext *dc, arg_NOT_U6 *a)
{
    tcg_gen_not_i32(cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_NOT_S(DisasContext *dc, arg_NOT_S *a)
{
    tcg_gen_not_i32(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
} 

static bool trans_AEX(DisasContext *dc, arg_AEX *a)
{
    TCGv_i32 result = tcg_temp_new_i32();
    gen_helper_aex(result, tcg_env, cpu_regs[a->c], cpu_regs[a->b]);
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    return true;
}

static bool trans_AEX_U6(DisasContext *dc, arg_AEX_U6 *a)
{
    TCGv_i32 result = tcg_temp_new_i32();
    gen_helper_aex(result, tcg_env, tcg_constant_i32(a->u), cpu_regs[a->b]);
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    return true;
}

static bool trans_AEX_S12(DisasContext *dc, arg_AEX_S12 *a)
{
    TCGv_i32 result = tcg_temp_new_i32();
    gen_helper_aex(result, tcg_env, tcg_constant_i32(a->s), cpu_regs[a->b]);
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    return true;
}

static bool trans_AEX_CC(DisasContext *dc, arg_AEX_CC *a)
{
    TCGv_i32 result = tcg_temp_new_i32();
    gen_helper_aex(result, tcg_env, cpu_regs[a->c], cpu_regs[a->b]);
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    return true;
}

static bool trans_AEX_CC_U6(DisasContext *dc, arg_AEX_CC_U6 *a)
{
    TCGv_i32 result = tcg_temp_new_i32();
    gen_helper_aex(result, tcg_env, tcg_constant_i32(a->u), cpu_regs[a->b]);
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    return true;
}

static bool trans_ABS(DisasContext *dc, arg_ABS *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_abs_i32(cpu_regs[a->b], orig_c);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_nf, orig_c, 0x80000000);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_vf, orig_c, 0x80000000);
    }
      return true;
}

static bool trans_ABS_U6(DisasContext *dc, arg_ABS_U6 *a)
{
    TCGv_i32 orig_u = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_u, tcg_constant_i32(a->u));
    tcg_gen_abs_i32(cpu_regs[a->b], orig_u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_nf, orig_u, 0x80000000);
        tcg_gen_shri_i32(cpu_cf, orig_u, 31);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_vf, orig_u, 0x80000000);
    }
      return true;
}

static bool trans_ABS_S(DisasContext *dc, arg_ABS_S *a)
{
    tcg_gen_abs_i32(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_ADC(DisasContext *dc, arg_ADC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_add2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), cpu_regs[a->c], tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_add2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->a], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_48 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_48 = tcg_temp_new_i32();
        TCGv_i32 vf_res_48 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_48, orig_b, cpu_regs[a->c]);
        tcg_gen_not_i32(vf_t0_48, vf_t0_48);
        tcg_gen_xor_i32(vf_t1_48, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_48, vf_t0_48, vf_t1_48);
        tcg_gen_shri_i32(vf_res_48, vf_t0_48, 31);
        TCGv_i32 vflag = vf_res_48;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_ADC_U6(DisasContext *dc, arg_ADC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_add2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), tcg_constant_i32(a->u), tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_add2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->a], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_49 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_49 = tcg_temp_new_i32();
        TCGv_i32 vf_res_49 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_49, orig_b, tcg_constant_i32(a->u));
        tcg_gen_not_i32(vf_t0_49, vf_t0_49);
        tcg_gen_xor_i32(vf_t1_49, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_49, vf_t0_49, vf_t1_49);
        tcg_gen_shri_i32(vf_res_49, vf_t0_49, 31);
        TCGv_i32 vflag = vf_res_49;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_ADC_S12(DisasContext *dc, arg_ADC_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_add2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), tcg_constant_i32(a->s), tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_add2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->a], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_50 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_50 = tcg_temp_new_i32();
        TCGv_i32 vf_res_50 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_50, orig_b, tcg_constant_i32(a->s));
        tcg_gen_not_i32(vf_t0_50, vf_t0_50);
        tcg_gen_xor_i32(vf_t1_50, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_50, vf_t0_50, vf_t1_50);
        tcg_gen_shri_i32(vf_res_50, vf_t0_50, 31);
        TCGv_i32 vflag = vf_res_50;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_ADC_CC(DisasContext *dc, arg_ADC_CC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_add2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), cpu_regs[a->c], tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_add2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_51 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_51 = tcg_temp_new_i32();
        TCGv_i32 vf_res_51 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_51, orig_b, cpu_regs[a->c]);
        tcg_gen_not_i32(vf_t0_51, vf_t0_51);
        tcg_gen_xor_i32(vf_t1_51, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_51, vf_t0_51, vf_t1_51);
        tcg_gen_shri_i32(vf_res_51, vf_t0_51, 31);
        TCGv_i32 vflag = vf_res_51;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_ADC_CC_U6(DisasContext *dc, arg_ADC_CC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_add2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), tcg_constant_i32(a->u), tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_add2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_52 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_52 = tcg_temp_new_i32();
        TCGv_i32 vf_res_52 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_52, orig_b, tcg_constant_i32(a->u));
        tcg_gen_not_i32(vf_t0_52, vf_t0_52);
        tcg_gen_xor_i32(vf_t1_52, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_52, vf_t0_52, vf_t1_52);
        tcg_gen_shri_i32(vf_res_52, vf_t0_52, 31);
        TCGv_i32 vflag = vf_res_52;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_ASR16(DisasContext *dc, arg_ASR16 *a)
{
    tcg_gen_sari_i32(cpu_regs[a->b], cpu_regs[a->c], 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_ASR16_U6(DisasContext *dc, arg_ASR16_U6 *a)
{
    tcg_gen_sari_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_ASR8(DisasContext *dc, arg_ASR8 *a)
{
    tcg_gen_sari_i32(cpu_regs[a->b], cpu_regs[a->c], 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_ASR8_U6(DisasContext *dc, arg_ASR8_U6 *a)
{
    tcg_gen_sari_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_LSR16(DisasContext *dc, arg_LSR16 *a)
{
    tcg_gen_shri_i32(cpu_regs[a->b], cpu_regs[a->c], 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
    }
    return true;
}

static bool trans_LSR16_U6(DisasContext *dc, arg_LSR16_U6 *a)
{
    tcg_gen_shri_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
    }
    return true;
}

static bool trans_LSR8(DisasContext *dc, arg_LSR8 *a)
{
    tcg_gen_shri_i32(cpu_regs[a->b], cpu_regs[a->c], 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
    }
    return true;
}

static bool trans_LSR8_U6(DisasContext *dc, arg_LSR8_U6 *a)
{
    tcg_gen_shri_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
    }
    return true;
}

static bool trans_LSL16(DisasContext *dc, arg_LSL16 *a)
{
    tcg_gen_shli_i32(cpu_regs[a->b], cpu_regs[a->c], 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_LSL16_U6(DisasContext *dc, arg_LSL16_U6 *a)
{
    tcg_gen_shli_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}


static bool trans_LSL8(DisasContext *dc, arg_LSL8 *a)
{
    tcg_gen_shli_i32(cpu_regs[a->b], cpu_regs[a->c], 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_LSL8_U6(DisasContext *dc, arg_LSL8_U6 *a)
{
    tcg_gen_shli_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_ROR8(DisasContext *dc, arg_ROR8 *a)
{
    tcg_gen_rotri_i32(cpu_regs[a->b], cpu_regs[a->c], 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_ROR8_U6(DisasContext *dc, arg_ROR8_U6 *a)
{
    tcg_gen_rotri_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 8);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_ROL(DisasContext *dc, arg_ROL *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_rotli_i32(cpu_regs[a->b], cpu_regs[a->c], 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
    return true;
}

static bool trans_ROL_U6(DisasContext *dc, arg_ROL_U6 *a)
{
    TCGv_i32 orig_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(orig_u, a->u);
    tcg_gen_rotli_i32(cpu_regs[a->b], orig_u, 1);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_u, 31);
    }
    return true;
}

static bool trans_MAX(DisasContext *dc, arg_MAX *a)
{
    tcg_gen_smax_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_sub_i32(alu, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, cpu_regs[a->c], cpu_regs[a->b]);
        TCGv_i32 vf_t0_53 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_53 = tcg_temp_new_i32();
        TCGv_i32 vf_res_53 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_53, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_53, cpu_regs[a->b], alu);
        tcg_gen_and_i32(vf_t0_53, vf_t0_53, vf_t1_53);
        tcg_gen_shri_i32(vf_res_53, vf_t0_53, 31);
        TCGv_i32 vflag = vf_res_53;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MAX_U6(DisasContext *dc, arg_MAX_U6 *a)
{
    tcg_gen_smax_i32(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_subi_i32(alu, cpu_regs[a->b], a->u);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcondi_i32(TCG_COND_LEU, cpu_cf, cpu_regs[a->b], a->u);
        TCGv_i32 vf_t0_54 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_54 = tcg_temp_new_i32();
        TCGv_i32 vf_res_54 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_54, cpu_regs[a->b], tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_54, cpu_regs[a->b], alu);
        tcg_gen_and_i32(vf_t0_54, vf_t0_54, vf_t1_54);
        tcg_gen_shri_i32(vf_res_54, vf_t0_54, 31);
        TCGv_i32 vflag = vf_res_54;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MAX_S12(DisasContext *dc, arg_MAX_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_smax_i32(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s));
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_subi_i32(alu, orig_b, a->s);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcondi_i32(TCG_COND_LEU, cpu_cf, orig_b, a->s);
        TCGv_i32 vf_t0_55 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_55 = tcg_temp_new_i32();
        TCGv_i32 vf_res_55 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_55, orig_b, tcg_constant_i32(a->s));
        tcg_gen_xor_i32(vf_t1_55, orig_b, alu);
        tcg_gen_and_i32(vf_t0_55, vf_t0_55, vf_t1_55);
        tcg_gen_shri_i32(vf_res_55, vf_t0_55, 31);
        TCGv_i32 vflag = vf_res_55;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MAX_CC(DisasContext *dc, arg_MAX_CC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_smax_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_sub_i32(alu, orig_b, cpu_regs[a->c]);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, cpu_regs[a->c], orig_b);
        TCGv_i32 vf_t0_56 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_56 = tcg_temp_new_i32();
        TCGv_i32 vf_res_56 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_56, orig_b, cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_56, orig_b, alu);
        tcg_gen_and_i32(vf_t0_56, vf_t0_56, vf_t1_56);
        tcg_gen_shri_i32(vf_res_56, vf_t0_56, 31);
        TCGv_i32 vflag = vf_res_56;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MAX_CC_U6(DisasContext *dc, arg_MAX_CC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_smax_i32(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_subi_i32(alu, orig_b, a->u);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcondi_i32(TCG_COND_LEU, cpu_cf, orig_b, a->u);
        TCGv_i32 vf_t0_57 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_57 = tcg_temp_new_i32();
        TCGv_i32 vf_res_57 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_57, orig_b, tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_57, orig_b, alu);
        tcg_gen_and_i32(vf_t0_57, vf_t0_57, vf_t1_57);
        tcg_gen_shri_i32(vf_res_57, vf_t0_57, 31);
        TCGv_i32 vflag = vf_res_57;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MIN(DisasContext *dc, arg_MIN *a)
{
    tcg_gen_smin_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_sub_i32(alu, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf,  cpu_regs[a->b], cpu_regs[a->c]);
        TCGv_i32 vf_t0_58 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_58 = tcg_temp_new_i32();
        TCGv_i32 vf_res_58 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_58, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_58, cpu_regs[a->b], alu);
        tcg_gen_and_i32(vf_t0_58, vf_t0_58, vf_t1_58);
        tcg_gen_shri_i32(vf_res_58, vf_t0_58, 31);
        TCGv_i32 vflag = vf_res_58;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MIN_U6(DisasContext *dc, arg_MIN_U6 *a)
{
    tcg_gen_smin_i32(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_subi_i32(alu, cpu_regs[a->b], a->u);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_cf,  cpu_regs[a->b], a->u);
        TCGv_i32 vf_t0_59 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_59 = tcg_temp_new_i32();
        TCGv_i32 vf_res_59 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_59, cpu_regs[a->b], tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_59, cpu_regs[a->b], alu);
        tcg_gen_and_i32(vf_t0_59, vf_t0_59, vf_t1_59);
        tcg_gen_shri_i32(vf_res_59, vf_t0_59, 31);
        TCGv_i32 vflag = vf_res_59;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MIN_S12(DisasContext *dc, arg_MIN_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_smin_i32(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s));
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_subi_i32(alu, orig_b, a->s);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_cf, orig_b, a->s);
        TCGv_i32 vf_t0_60 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_60 = tcg_temp_new_i32();
        TCGv_i32 vf_res_60 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_60, orig_b, tcg_constant_i32(a->s));
        tcg_gen_xor_i32(vf_t1_60, orig_b, alu);
        tcg_gen_and_i32(vf_t0_60, vf_t0_60, vf_t1_60);
        tcg_gen_shri_i32(vf_res_60, vf_t0_60, 31);
        TCGv_i32 vflag = vf_res_60;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MIN_CC(DisasContext *dc, arg_MIN_CC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_smin_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_sub_i32(alu, orig_b, cpu_regs[a->c]);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, orig_b, cpu_regs[a->c]);
        TCGv_i32 vf_t0_61 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_61 = tcg_temp_new_i32();
        TCGv_i32 vf_res_61 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_61, orig_b, cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_61, orig_b, alu);
        tcg_gen_and_i32(vf_t0_61, vf_t0_61, vf_t1_61);
        tcg_gen_shri_i32(vf_res_61, vf_t0_61, 31);
        TCGv_i32 vflag = vf_res_61;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_MIN_CC_U6(DisasContext *dc, arg_MIN_CC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_smin_i32(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_subi_i32(alu, orig_b, a->u);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_cf, orig_b, a->u);
        TCGv_i32 vf_t0_62 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_62 = tcg_temp_new_i32();
        TCGv_i32 vf_res_62 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_62, orig_b, tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_62, orig_b, alu);
        tcg_gen_and_i32(vf_t0_62, vf_t0_62, vf_t1_62);
        tcg_gen_shri_i32(vf_res_62, vf_t0_62, 31);
        TCGv_i32 vflag = vf_res_62;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_SEXB(DisasContext *dc, arg_SEXB *a)
{
    tcg_gen_ext8s_i32(cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_SEXB_U6(DisasContext *dc, arg_SEXB_U6 *a)
{
    tcg_gen_ext8s_i32(cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_SEXB_S(DisasContext *dc, arg_SEXB_S *a)
{
    tcg_gen_ext8s_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_SEXH(DisasContext *dc, arg_SEXH *a)
{
    tcg_gen_ext16s_i32(cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_SEXH_U6(DisasContext *dc, arg_SEXH_U6 *a)
{
    tcg_gen_ext16s_i32(cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_SEXH_S(DisasContext *dc, arg_SEXH_S *a)
{
    tcg_gen_ext16s_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_EXTB(DisasContext *dc, arg_EXTB *a)
{
    tcg_gen_ext8u_i32(cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_mov_i32(cpu_nf, tcg_constant_i32(0));
    }
    return true;
}

static bool trans_EXTB_U6(DisasContext *dc, arg_EXTB_U6 *a)
{
    tcg_gen_ext8u_i32(cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_mov_i32(cpu_nf, tcg_constant_i32(0));
    }
    return true;
}

static bool trans_EXTB_S(DisasContext *dc, arg_EXTB_S *a)
{
    tcg_gen_ext8u_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_EXTH(DisasContext *dc, arg_EXTH *a)
{
    tcg_gen_ext16u_i32(cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_mov_i32(cpu_nf, tcg_constant_i32(0));
    }
    return true;
}

static bool trans_EXTH_U6(DisasContext *dc, arg_EXTH_U6 *a)
{
    tcg_gen_ext16u_i32(cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_mov_i32(cpu_nf, tcg_constant_i32(0));
    }
    return true;
}

static bool trans_EXTH_S(DisasContext *dc, arg_EXTH_S *a)
{
    tcg_gen_ext16u_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_RCMP(DisasContext *dc, arg_rcmp *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->c], cpu_regs[a->b]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->c], cpu_regs[a->b]);
    TCGv_i32 vf_t0_63 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_63 = tcg_temp_new_i32();
    TCGv_i32 vf_res_63 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_63, cpu_regs[a->c], cpu_regs[a->b]);
    tcg_gen_xor_i32(vf_t1_63, cpu_regs[a->c], tmp);
    tcg_gen_and_i32(vf_t0_63, vf_t0_63, vf_t1_63);
    tcg_gen_shri_i32(vf_res_63, vf_t0_63, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_63);
    return true;
}

static bool trans_RCMP_U6(DisasContext *dc, arg_rcmp_u6 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, tcg_constant_i32(a->u), cpu_regs[a->b]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, tcg_constant_i32(a->u), cpu_regs[a->b]);
    TCGv_i32 vf_t0_64 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_64 = tcg_temp_new_i32();
    TCGv_i32 vf_res_64 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_64, tcg_constant_i32(a->u), cpu_regs[a->b]);
    tcg_gen_xor_i32(vf_t1_64, tcg_constant_i32(a->u), tmp);
    tcg_gen_and_i32(vf_t0_64, vf_t0_64, vf_t1_64);
    tcg_gen_shri_i32(vf_res_64, vf_t0_64, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_64);
    return true;
}

static bool trans_RCMP_S12(DisasContext *dc, arg_rcmp_s12 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, tcg_constant_i32(a->s), cpu_regs[a->b]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, tcg_constant_i32(a->s), cpu_regs[a->b]);
    TCGv_i32 vf_t0_65 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_65 = tcg_temp_new_i32();
    TCGv_i32 vf_res_65 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_65, tcg_constant_i32(a->s), cpu_regs[a->b]);
    tcg_gen_xor_i32(vf_t1_65, tcg_constant_i32(a->s), tmp);
    tcg_gen_and_i32(vf_t0_65, vf_t0_65, vf_t1_65);
    tcg_gen_shri_i32(vf_res_65, vf_t0_65, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_65);
    return true;
}

static bool trans_RCMP_CC(DisasContext *dc, arg_rcmp_cc *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->c], cpu_regs[a->b]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->c], cpu_regs[a->b]);
    TCGv_i32 vf_t0_66 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_66 = tcg_temp_new_i32();
    TCGv_i32 vf_res_66 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_66, cpu_regs[a->c], cpu_regs[a->b]);
    tcg_gen_xor_i32(vf_t1_66, cpu_regs[a->c], tmp);
    tcg_gen_and_i32(vf_t0_66, vf_t0_66, vf_t1_66);
    tcg_gen_shri_i32(vf_res_66, vf_t0_66, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_66);
    return true;
}

static bool trans_RCMP_CC_U6(DisasContext *dc, arg_rcmp_cc_u6 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, tcg_constant_i32(a->u), cpu_regs[a->b]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, tcg_constant_i32(a->u), cpu_regs[a->b]);
    TCGv_i32 vf_t0_67 = tcg_temp_new_i32();
    TCGv_i32 vf_t1_67 = tcg_temp_new_i32();
    TCGv_i32 vf_res_67 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0_67, tcg_constant_i32(a->u), cpu_regs[a->b]);
    tcg_gen_xor_i32(vf_t1_67, tcg_constant_i32(a->u), tmp);
    tcg_gen_and_i32(vf_t0_67, vf_t0_67, vf_t1_67);
    tcg_gen_shri_i32(vf_res_67, vf_t0_67, 31);
    tcg_gen_mov_i32(cpu_vf, vf_res_67);
    return true;
}

static bool trans_SBC(DisasContext *dc, arg_SBC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_sub2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), cpu_regs[a->c], tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_sub2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->a], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_68 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_68 = tcg_temp_new_i32();
        TCGv_i32 vf_res_68 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_68, orig_b, cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_68, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_68, vf_t0_68, vf_t1_68);
        tcg_gen_shri_i32(vf_res_68, vf_t0_68, 31);
        TCGv_i32 vflag = vf_res_68;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_SBC_U6(DisasContext *dc, arg_SBC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_sub2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), tcg_constant_i32(a->u), tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_sub2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->a], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_69 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_69 = tcg_temp_new_i32();
        TCGv_i32 vf_res_69 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_69, orig_b, tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_69, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_69, vf_t0_69, vf_t1_69);
        tcg_gen_shri_i32(vf_res_69, vf_t0_69, 31);
        TCGv_i32 vflag = vf_res_69;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_SBC_S12(DisasContext *dc, arg_SBC_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_sub2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), tcg_constant_i32(a->s), tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_sub2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->a], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_70 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_70 = tcg_temp_new_i32();
        TCGv_i32 vf_res_70 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_70, orig_b, tcg_constant_i32(a->s));
        tcg_gen_xor_i32(vf_t1_70, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_70, vf_t0_70, vf_t1_70);
        tcg_gen_shri_i32(vf_res_70, vf_t0_70, 31);
        TCGv_i32 vflag = vf_res_70;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_SBC_CC(DisasContext *dc, arg_SBC_CC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_sub2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), cpu_regs[a->c], tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_sub2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_71 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_71 = tcg_temp_new_i32();
        TCGv_i32 vf_res_71 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_71, orig_b, cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_71, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_71, vf_t0_71, vf_t1_71);
        tcg_gen_shri_i32(vf_res_71, vf_t0_71, 31);
        TCGv_i32 vflag = vf_res_71;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_SBC_CC_U6(DisasContext *dc, arg_SBC_CC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_sub2_i32(sum_lo, sum_hi, cpu_regs[a->b], tcg_constant_i32(0), tcg_constant_i32(a->u), tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_sub2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_regs[a->b], result);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0_72 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_72 = tcg_temp_new_i32();
        TCGv_i32 vf_res_72 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_72, orig_b, tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_72, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_72, vf_t0_72, vf_t1_72);
        tcg_gen_shri_i32(vf_res_72, vf_t0_72, 31);
        TCGv_i32 vflag = vf_res_72;
        tcg_gen_mov_i32(cpu_vf, vflag);
    }
    return true;
}

static bool trans_BIC(DisasContext *dc, arg_BIC *a)
{
    tcg_gen_andc_i32(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BIC_U6(DisasContext *dc, arg_BIC_U6 *a)
{
    tcg_gen_andc_i32(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BIC_S12(DisasContext *dc, arg_BIC_S12 *a)
{
    tcg_gen_andc_i32(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BIC_CC(DisasContext *dc, arg_BIC_CC *a)
{
    tcg_gen_andc_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BIC_CC_U6(DisasContext *dc, arg_BIC_CC_U6 *a)
{
    tcg_gen_andc_i32(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BIC_S(DisasContext *dc, arg_BIC_S *a)
{
    tcg_gen_andc_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_TST(DisasContext *dc, arg_TST *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_TST_U6(DisasContext *dc, arg_TST_U6 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_TST_S12(DisasContext *dc, arg_TST_S12 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->s));
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_TST_CC(DisasContext *dc, arg_TST_CC *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_TST_CC_U6(DisasContext *dc, arg_TST_CC_U6 *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_TST_S(DisasContext *dc, arg_TST_S *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_BTST(DisasContext *dc, arg_BTST *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, cpu_regs[a->b], mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BTST_U6(DisasContext *dc, arg_BTST_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, cpu_regs[a->b], mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BTST_S12(DisasContext *dc, arg_BTST_S12 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->s), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, cpu_regs[a->b], mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BTST_CC(DisasContext *dc, arg_BTST_CC *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, cpu_regs[a->b], mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BTST_CC_U6(DisasContext *dc, arg_BTST_CC_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, cpu_regs[a->b], mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BTST_S(DisasContext *dc, arg_BTST_S *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, cpu_regs[a->b], mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BSET(DisasContext *dc, arg_BSET *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_movi_i32(cpu_zf, 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BSET_U6(DisasContext *dc, arg_BSET_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_movi_i32(cpu_zf, 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BSET_S12(DisasContext *dc, arg_BSET_S12 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->s), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_movi_i32(cpu_zf, 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BSET_CC(DisasContext *dc, arg_BSET_CC *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_movi_i32(cpu_zf, 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BSET_CC_U6(DisasContext *dc, arg_BSET_CC_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_movi_i32(cpu_zf, 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BSET_S(DisasContext *dc, arg_BSET_S *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    return true;
}

static bool trans_BCLR(DisasContext *dc, arg_BCLR *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BCLR_U6(DisasContext *dc, arg_BCLR_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BCLR_S12(DisasContext *dc, arg_BCLR_S12 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->s), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BCLR_CC(DisasContext *dc, arg_BCLR_CC *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BCLR_CC_U6(DisasContext *dc, arg_BCLR_CC_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BCLR_S(DisasContext *dc, arg_BCLR_S *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    return true;
}

static bool trans_BXOR(DisasContext *dc, arg_BXOR *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_xor_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BXOR_U6(DisasContext *dc, arg_BXOR_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_xor_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BXOR_S12(DisasContext *dc, arg_BXOR_S12 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->s), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_xor_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BXOR_CC(DisasContext *dc, arg_BXOR_CC *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_xor_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BXOR_CC_U6(DisasContext *dc, arg_BXOR_CC_U6 *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_xor_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BMSK(DisasContext *dc, arg_BMSK *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    TCGv_i32 inv = tcg_temp_new_i32();
    tcg_gen_subfi_i32(inv, 31, bitpos);
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_shr_i32(mask, tcg_constant_i32(0xFFFFFFFF), inv);
    tcg_gen_and_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;
}

static bool trans_BMSK_U6(DisasContext *dc, arg_BMSK_U6 *a)
{
    uint32_t mask = (uint32_t)0xFFFFFFFF >> (31 - (a->u & 31));
    tcg_gen_andi_i32(cpu_regs[a->a], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
    }
    return true;

}

static bool trans_BMSK_S12(DisasContext *dc, arg_BMSK_S12 *a)
{
    uint32_t mask = (uint32_t)0xFFFFFFFF >> (31 - (a->s & 31));
    tcg_gen_andi_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BMSK_CC(DisasContext *dc, arg_BMSK_CC *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, cpu_regs[a->c], 31);
    TCGv_i32 inv = tcg_temp_new_i32();
    tcg_gen_subfi_i32(inv, 31, bitpos);
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_shr_i32(mask, tcg_constant_i32(0xFFFFFFFF), inv);
    tcg_gen_and_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BMSK_CC_U6(DisasContext *dc, arg_BMSK_CC_U6 *a)
{
    uint32_t mask = (uint32_t)0xFFFFFFFF >> (31 - (a->u & 31));
    tcg_gen_andi_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_BMSK_S(DisasContext *dc, arg_BMSK_S *a)
{
    uint32_t mask = (uint32_t)0xFFFFFFFF >> (31 - (a->u & 31));
    tcg_gen_andi_i32(cpu_regs[a->b], cpu_regs[a->b], mask);
    return true;

}

static bool trans_NEG_S(DisasContext *dc, arg_NEG_S *a)
{
    tcg_gen_neg_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_SWAP(DisasContext *dc, arg_SWAP *a)
{
    tcg_gen_rotli_i32(cpu_regs[a->b], cpu_regs[a->c], 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_SWAP_U6(DisasContext *dc, arg_SWAP_U6 *a)
{
    tcg_gen_rotli_i32(cpu_regs[a->b], tcg_constant_i32(a->u), 16);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_SWAPE(DisasContext *dc, arg_SWAPE *a)
{
    tcg_gen_bswap32_i32(cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_SWAPE_U6(DisasContext *dc, arg_SWAPE_U6 *a)
{
    tcg_gen_bswap32_i32(cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_RLC(DisasContext *dc, arg_RLC *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    TCGv_i32 shifted = tcg_temp_new_i32();
    tcg_gen_shli_i32(shifted, orig_c, 1);
    tcg_gen_or_i32(cpu_regs[a->b], shifted, cpu_cf);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
    return true;
}

static bool trans_RLC_U6(DisasContext *dc, arg_RLC_U6 *a)
{
    TCGv_i32 orig_u = tcg_temp_new_i32();
    tcg_gen_movi_i32(orig_u, a->u);
    TCGv_i32 shifted = tcg_temp_new_i32();
    tcg_gen_shli_i32(shifted, orig_u, 1);
    tcg_gen_or_i32(cpu_regs[a->b], shifted, cpu_cf);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_u, 31);
    }
    return true;
}

static bool trans_RRC(DisasContext *dc, arg_RRC *a)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    TCGv_i32 shifted = tcg_temp_new_i32();
    tcg_gen_shri_i32(shifted, orig_c, 1);
    TCGv_i32 carry_hi = tcg_temp_new_i32();
    tcg_gen_shli_i32(carry_hi, cpu_cf, 31);
    tcg_gen_or_i32(cpu_regs[a->b], shifted, carry_hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
    return true;
}

static bool trans_RRC_U6(DisasContext *dc, arg_RRC_U6 *a)
{
    TCGv_i32 orig_u = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_u, tcg_constant_i32(a->u));
    TCGv_i32 shifted = tcg_temp_new_i32();
    tcg_gen_shri_i32(shifted, orig_u, 1);
    TCGv_i32 carry_hi = tcg_temp_new_i32();
    tcg_gen_shli_i32(carry_hi, cpu_cf, 31);
    tcg_gen_or_i32(cpu_regs[a->b], shifted, carry_hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_shri_i32(cpu_cf, orig_u, 31);
    }
    return true;
}

static bool trans_MPYM(DisasContext *dc, arg_MPYM *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->a], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYM_U6(DisasContext *dc, arg_MPYM_U6 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_mov_i32(cpu_regs[a->a], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYM_S12(DisasContext *dc, arg_MPYM_S12 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->s));
    tcg_gen_mov_i32(cpu_regs[a->b], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYM_CC(DisasContext *dc, arg_MPYM_CC *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->b], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYM_CC_U6(DisasContext *dc, arg_MPYM_CC_U6 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_mov_i32(cpu_regs[a->b], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYMU(DisasContext *dc, arg_MPYMU *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->a], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYMU_U6(DisasContext *dc, arg_MPYMU_U6 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_mov_i32(cpu_regs[a->a], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYMU_S12(DisasContext *dc, arg_MPYMU_S12 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->s));
    tcg_gen_mov_i32(cpu_regs[a->b], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYMU_CC(DisasContext *dc, arg_MPYMU_CC *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->b], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYMU_CC_U6(DisasContext *dc, arg_MPYMU_CC_U6 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_mov_i32(cpu_regs[a->b], hi);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
    return true;
}

static bool trans_MPYU(DisasContext *dc, arg_MPYU *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->a], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_vf, hi, 0);
    }
    return true;
}

static bool trans_MPYU_U6(DisasContext *dc, arg_MPYU_U6 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_mov_i32(cpu_regs[a->a], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_vf, hi, 0);
    }
    return true;
}

static bool trans_MPYU_S12(DisasContext *dc, arg_MPYU_S12 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->s));
    tcg_gen_mov_i32(cpu_regs[a->b], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_vf, hi, 0);
    }
    return true;
}

static bool trans_MPYU_CC(DisasContext *dc, arg_MPYU_CC *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->b], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_vf, hi, 0);
    }
    return true;
}

static bool trans_MPYU_CC_U6(DisasContext *dc, arg_MPYU_CC_U6 *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
    tcg_gen_mov_i32(cpu_regs[a->b], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_vf, hi, 0);
    }
    return true;
}

static bool trans_RSUB(DisasContext *dc, arg_RSUB *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_sub_i32(cpu_regs[a->a], cpu_regs[a->c], cpu_regs[a->b]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_c, orig_b);
        TCGv_i32 vf_t0_73 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_73 = tcg_temp_new_i32();
        TCGv_i32 vf_res_73 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_73, orig_c, orig_b);
        tcg_gen_xor_i32(vf_t1_73, orig_c, cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_73, vf_t0_73, vf_t1_73);
        tcg_gen_shri_i32(vf_res_73, vf_t0_73, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_73);
    }
    return true;
}

static bool trans_RSUB_U6(DisasContext *dc, arg_RSUB_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->a], tcg_constant_i32(a->u), cpu_regs[a->b]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->a], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->a], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, tcg_constant_i32(a->u), orig_b);
        TCGv_i32 vf_t0_74 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_74 = tcg_temp_new_i32();
        TCGv_i32 vf_res_74 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_74, tcg_constant_i32(a->u), orig_b);
        tcg_gen_xor_i32(vf_t1_74, tcg_constant_i32(a->u), cpu_regs[a->a]);
        tcg_gen_and_i32(vf_t0_74, vf_t0_74, vf_t1_74);
        tcg_gen_shri_i32(vf_res_74, vf_t0_74, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_74);
    }
    return true;
}

static bool trans_RSUB_S12(DisasContext *dc, arg_RSUB_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->b], tcg_constant_i32(a->s), cpu_regs[a->b]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, tcg_constant_i32(a->s), orig_b);
        TCGv_i32 vf_t0_75 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_75 = tcg_temp_new_i32();
        TCGv_i32 vf_res_75 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_75, tcg_constant_i32(a->s), orig_b);
        tcg_gen_xor_i32(vf_t1_75, tcg_constant_i32(a->s), cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_75, vf_t0_75, vf_t1_75);
        tcg_gen_shri_i32(vf_res_75, vf_t0_75, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_75);
    }
    return true;
}

static bool trans_RSUB_CC(DisasContext *dc, arg_RSUB_CC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, cpu_regs[a->c]);
    tcg_gen_sub_i32(cpu_regs[a->b], cpu_regs[a->c], cpu_regs[a->b]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_c, orig_b);
        TCGv_i32 vf_t0_76 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_76 = tcg_temp_new_i32();
        TCGv_i32 vf_res_76 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_76, orig_c, orig_b);
        tcg_gen_xor_i32(vf_t1_76, orig_c, cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_76, vf_t0_76, vf_t1_76);
        tcg_gen_shri_i32(vf_res_76, vf_t0_76, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_76);
    }
    return true;
}

static bool trans_RSUB_CC_U6(DisasContext *dc, arg_RSUB_CC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_sub_i32(cpu_regs[a->b], tcg_constant_i32(a->u), cpu_regs[a->b]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, tcg_constant_i32(a->u), orig_b);
        TCGv_i32 vf_t0_77 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_77 = tcg_temp_new_i32();
        TCGv_i32 vf_res_77 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_77, tcg_constant_i32(a->u), orig_b);
        tcg_gen_xor_i32(vf_t1_77, tcg_constant_i32(a->u), cpu_regs[a->b]);
        tcg_gen_and_i32(vf_t0_77, vf_t0_77, vf_t1_77);
        tcg_gen_shri_i32(vf_res_77, vf_t0_77, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_77);
    }
    return true;
}

static bool trans_SET(DisasContext *dc, arg_SET *a)
{
    if (a->f) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
        tcg_gen_shri_i32(cpu_nf, tmp, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], cpu_regs[a->c]);
        TCGv_i32 vf_t0_78 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_78 = tcg_temp_new_i32();
        TCGv_i32 vf_res_78 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_78, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_78, cpu_regs[a->b], tmp);
        tcg_gen_and_i32(vf_t0_78, vf_t0_78, vf_t1_78);
        tcg_gen_shri_i32(vf_res_78, vf_t0_78, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_78);
    }
     tcg_gen_setcond_i32(setcc_conds[a->i], cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_SET_U6(DisasContext *dc, arg_SET_U6 *a)
{
    if (a->f) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->u));
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
        tcg_gen_shri_i32(cpu_nf, tmp, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], tcg_constant_i32(a->u));
        TCGv_i32 vf_t0_79 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_79 = tcg_temp_new_i32();
        TCGv_i32 vf_res_79 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_79, cpu_regs[a->b], tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_79, cpu_regs[a->b], tmp);
        tcg_gen_and_i32(vf_t0_79, vf_t0_79, vf_t1_79);
        tcg_gen_shri_i32(vf_res_79, vf_t0_79, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_79);
    }
    tcg_gen_setcond_i32(setcc_conds[a->i], cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_SET_S12(DisasContext *dc, arg_SET_S12 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_setcond_i32(setcc_conds[a->i], cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s));
    if (a->f) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, orig_b, tcg_constant_i32(a->s));
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
        tcg_gen_shri_i32(cpu_nf, tmp, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, tcg_constant_i32(a->s));
        TCGv_i32 vf_t0_80 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_80 = tcg_temp_new_i32();
        TCGv_i32 vf_res_80 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_80, orig_b, tcg_constant_i32(a->s));
        tcg_gen_xor_i32(vf_t1_80, orig_b, tmp);
        tcg_gen_and_i32(vf_t0_80, vf_t0_80, vf_t1_80);
        tcg_gen_shri_i32(vf_res_80, vf_t0_80, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_80);
    }
    return true;
}

static bool trans_SET_CC(DisasContext *dc, arg_SET_CC *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_setcond_i32(setcc_conds[a->i], cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, orig_b, cpu_regs[a->c]);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
        tcg_gen_shri_i32(cpu_nf, tmp, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, cpu_regs[a->c]);
        TCGv_i32 vf_t0_81 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_81 = tcg_temp_new_i32();
        TCGv_i32 vf_res_81 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_81, orig_b, cpu_regs[a->c]);
        tcg_gen_xor_i32(vf_t1_81, orig_b, tmp);
        tcg_gen_and_i32(vf_t0_81, vf_t0_81, vf_t1_81);
        tcg_gen_shri_i32(vf_res_81, vf_t0_81, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_81);
    }
    return true;
}

static bool trans_SET_CC_U6(DisasContext *dc, arg_SET_CC_U6 *a)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
    tcg_gen_setcond_i32(setcc_conds[a->i], cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u));
    if (a->f) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, orig_b, tcg_constant_i32(a->u));
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
        tcg_gen_shri_i32(cpu_nf, tmp, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, tcg_constant_i32(a->u));
        TCGv_i32 vf_t0_82 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_82 = tcg_temp_new_i32();
        TCGv_i32 vf_res_82 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_82, orig_b, tcg_constant_i32(a->u));
        tcg_gen_xor_i32(vf_t1_82, orig_b, tmp);
        tcg_gen_and_i32(vf_t0_82, vf_t0_82, vf_t1_82);
        tcg_gen_shri_i32(vf_res_82, vf_t0_82, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_82);
    }
    return true;
}

static void arc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    bool had_pending_delay_slot = dc->has_delay_slot;
    uint16_t insn_hi = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next, MO_LE);

    uint16_t op5 = (insn_hi >> 11) & 0x1F;
    if (op5 <= 0x0B) {
        uint16_t insn_lo = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next + 2, MO_LE);
        uint32_t insn = (insn_hi << 16) | insn_lo;
        TCGLabel *label_skip = gen_new_label();
        if (extract32(insn, 27, 5) == 0x04 && extract32(insn, 22, 2) == 3) {
            unsigned _q = extract32(insn, 0, 5);
            TCGv_i32 cond = gen_cc_test(_q);
            tcg_gen_brcondi_i32(TCG_COND_EQ, cond, 0, label_skip);
        }
        if (!decode(dc, insn)) {
            gen_helper_halt(tcg_env);
            dc->base.is_jmp = DISAS_NORETURN;
         }
        gen_set_label(label_skip);
        dc->base.pc_next +=4;
    } else {
        if (!decode16(dc, insn_hi)) {
            gen_helper_halt(tcg_env);
            dc->base.is_jmp = DISAS_NORETURN;
        }
        dc->base.pc_next += 2;
    }
    if (had_pending_delay_slot) {
        tcg_gen_mov_i32(cpu_pc, dc->delay_target);
        dc->base.is_jmp = DISAS_NORETURN;
        dc->has_delay_slot = false;
    }

}

static void arc_tr_init_disas_context(DisasContextBase *db, CPUState *cs) { }

static void arc_tr_tb_start(DisasContextBase *db, CPUState *cs) {}

static void arc_tr_insn_start(DisasContextBase *db, CPUState *cs)
{
    DisasContext *dc = container_of(db, DisasContext, base);
    tcg_gen_insn_start(dc->base.pc_next, 0, 0);
}
static void arc_tr_tb_stop(DisasContextBase *db, CPUState *cs) 
{ 
    DisasContext *dc = container_of(db, DisasContext, base);
    if (dc->base.is_jmp != DISAS_NORETURN) {
          tcg_gen_movi_i32(cpu_pc, dc->base.pc_next);
      }
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