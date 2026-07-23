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

void arc_translate_init(void)
{
    static const char * const regnames[] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "SP", "r29", "r30", "r31",
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
    if (a->q == 0) {
      tcg_gen_mov_i32(cpu_regs[a->b], cpu_regs[a->c]);
    } else if (a->q == 1) {
      tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cpu_zf, tcg_constant_i32(1), cpu_regs[a->c], cpu_regs[a->b]);
    }
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_MOV_CC_F_U6(DisasContext *dc, arg_MOV_CC_F_U6 *a)
{
    if (a->q == 0) {
        tcg_gen_movi_i32(cpu_regs[a->b], a->u);
    } else if (a->q == 1) {
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cpu_zf, tcg_constant_i32(1), tcg_constant_i32(a->u), cpu_regs[a->b]);
    }
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_MOV_S_H_S3(DisasContext *dc, arg_MOV_S_H_S3 *a)
{
    tcg_gen_movi_i32(cpu_regs[a->h], a->s);
    return true;
}

static bool trans_MOV_S_NE(DisasContext *dc, arg_MOV_S_NE *a)
{
    tcg_gen_movcond_i32(TCG_COND_NE, cpu_regs[arc_reduced_regs[a->b]],cpu_zf, tcg_constant_i32(0),cpu_regs[a->h], cpu_regs[a->b]);
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
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(t0, orig_b, cpu_regs[a->c]);
        tcg_gen_not_i32(t0, t0);
        tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(t0, t0, t1);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
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
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_xori_i32(t0, orig_b, a->u);
        tcg_gen_not_i32(t0, t0);
        tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(t0, t0, t1);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
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
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_xori_i32(t0, orig_b, a->s);
        tcg_gen_not_i32(t0, t0);
        tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(t0, t0, t1);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
    }
    return true;
}

static bool trans_ADD_CC_F(DisasContext *dc, arg_ADD_CC_F *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        tcg_gen_add_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_b);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xor_i32(t0, orig_b, cpu_regs[a->c]);
            tcg_gen_not_i32(t0, t0);
            tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->b]);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(cpu_vf, t0, 31);
        }
    } else if (a->q == 1) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xor_i32(t0, cpu_regs[a->b], cpu_regs[a->c]);
            tcg_gen_not_i32(t0, t0);
            tcg_gen_xor_i32(t1, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(new_v, t0, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(1), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(1), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(1), new_c, cpu_cf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_vf, cond, tcg_constant_i32(1), new_v, cpu_vf);
          }
          tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(1), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ADD_CC_F_U6(DisasContext *dc, arg_ADD_CC_F_U6 *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        tcg_gen_add_i32(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u));
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_b);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xor_i32(t0, orig_b, tcg_constant_i32(a->u));
            tcg_gen_not_i32(t0, t0);
            tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->b]);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(cpu_vf, t0, 31);
        }
    } else if (a->q == 1) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_regs[a->b], tcg_constant_i32(a->u));
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_LTU, new_c, tmp, cpu_regs[a->b]);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xor_i32(t0, cpu_regs[a->b], tcg_constant_i32(a->u));
            tcg_gen_not_i32(t0, t0);
            tcg_gen_xor_i32(t1, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(new_v, t0, 31);
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

static bool trans_MPY(DisasContext *dc, arg_MPY *a)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_mov_i32(cpu_regs[a->a], lo);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo, 0);
        tcg_gen_shri_i32(cpu_nf, hi, 31);
        TCGv_i32 sign_ext = tcg_temp_new_i32();
        tcg_gen_sari_i32(sign_ext, lo, 31);
        tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
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
        TCGv_i32 sign_ext = tcg_temp_new_i32();
        tcg_gen_sari_i32(sign_ext, lo, 31);
        tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
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
        TCGv_i32 sign_ext = tcg_temp_new_i32();
        tcg_gen_sari_i32(sign_ext, lo, 31);
        tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
    }
    return true;
}

static bool trans_MPY_CC_F(DisasContext *dc, arg_MPY_CC_F *a)
{
    if (a->q == 0) {
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], cpu_regs[a->c]);
        tcg_gen_mov_i32(cpu_regs[a->b], lo);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo, 0);
            tcg_gen_shri_i32(cpu_nf, hi, 31);
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
        }
    } else if (a->q == 1) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
    if (a->q == 0) {
        TCGv_i32 lo = tcg_temp_new_i32();
        TCGv_i32 hi = tcg_temp_new_i32();
        tcg_gen_muls2_i32(lo, hi, cpu_regs[a->b], tcg_constant_i32(a->u));
        tcg_gen_mov_i32(cpu_regs[a->b], lo);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo, 0);
            tcg_gen_shri_i32(cpu_nf, hi, 31);
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
        }
    } else if (a->q == 1) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(t0, orig_b, cpu_regs[a->c]);
        tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(t0, t0, t1);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
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
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, cpu_regs[a->a], orig_b);
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_xori_i32(t0, orig_b, a->u);
        tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->a]);
        tcg_gen_and_i32(t0, t0, t1);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
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
        tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_cf, orig_b, a->s);
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_xori_i32(t0, orig_b, a->s);
        tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->b]);
        tcg_gen_and_i32(t0, t0, t1);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
    }
    return true;
}

static bool trans_SUB_CC(DisasContext *dc, arg_SUB_CC *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        tcg_gen_sub_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
            tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, orig_b, cpu_regs[a->c]);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xor_i32(t0, orig_b, cpu_regs[a->c]);
            tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->b]);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(cpu_vf, t0, 31);
        }
    } else if (a->q == 1) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcond_i32(TCG_COND_GEU, new_c, cpu_regs[a->b], cpu_regs[a->c]);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xor_i32(t0, cpu_regs[a->b], cpu_regs[a->c]);
            tcg_gen_xor_i32(t1, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(new_v, t0, 31);
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
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        tcg_gen_subi_i32(cpu_regs[a->b], cpu_regs[a->b], a->u);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
            tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_cf, orig_b, a->u);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xori_i32(t0, orig_b, a->u);
            tcg_gen_xor_i32(t1, orig_b, cpu_regs[a->b]);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(cpu_vf, t0, 31);
        }
    } else if (a->q == 1) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_subi_i32(tmp, cpu_regs[a->b], a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            TCGv_i32 new_c = tcg_temp_new_i32();
            TCGv_i32 new_v = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_setcondi_i32(TCG_COND_GEU, new_c, cpu_regs[a->b], a->u);
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_xori_i32(t0, cpu_regs[a->b], a->u);
            tcg_gen_xor_i32(t1, cpu_regs[a->b], tmp);
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(new_v, t0, 31);
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
    tcg_gen_sub_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]]);
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
    if (a->q == 0) {
        tcg_gen_and_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_and_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_AND_CC_U6(DisasContext *dc, arg_AND_CC_U6 *a)
{
    if (a->q == 0) {
        tcg_gen_andi_i32(cpu_regs[a->b], cpu_regs[a->b], a->u);
        if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_andi_i32(tmp, cpu_regs[a->b], a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
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
    if (a->q == 0) {
        tcg_gen_or_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_or_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_OR_CC_U6(DisasContext *dc, arg_OR_CC_U6 *a)
{
    if (a->q == 0) {
        tcg_gen_ori_i32(cpu_regs[a->b], cpu_regs[a->b], a->u);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_ori_i32(tmp, cpu_regs[a->b], a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
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
    if (a->q == 0) {
        tcg_gen_xor_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_xor_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_XOR_CC_U6(DisasContext *dc, arg_XOR_CC_U6 *a)
{
    if (a->q == 0) {
        tcg_gen_xori_i32(cpu_regs[a->b], cpu_regs[a->b], a->u);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_xori_i32(tmp, cpu_regs[a->b],a->u);
        if (a->f) {
            TCGv_i32 new_z = tcg_temp_new_i32();
            TCGv_i32 new_n = tcg_temp_new_i32();
            tcg_gen_setcondi_i32(TCG_COND_EQ, new_z, tmp, 0);
            tcg_gen_shri_i32(new_n, tmp, 31);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
        }
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
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
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_c);
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_xor_i32(t0, orig_c, cpu_regs[a->b]);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
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
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], orig_c);
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_xor_i32(t0, orig_c, cpu_regs[a->b]);
        tcg_gen_shri_i32(cpu_vf, t0, 31);
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
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c], 31);
        tcg_gen_shl_i32(cpu_regs[a->b], cpu_regs[a->b], shift_amt);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_shr_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ASL_CC_U6(DisasContext *dc, arg_ASL_CC_U6 *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
        tcg_gen_shl_i32(cpu_regs[a->b], cpu_regs[a->b], shift_amt);
        if (a->f) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
            tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
            TCGv_i32 inv_amt = tcg_temp_new_i32();
            tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
            TCGv_i32 bit = tcg_temp_new_i32();
            tcg_gen_shr_i32(bit, orig_b, inv_amt);
            tcg_gen_andi_i32(bit, bit, 1);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
        }
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
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
    tcg_gen_shr_i32(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c]);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
    }
    return true;
}

static bool trans_LSR_U6(DisasContext *dc, arg_LSR_U6 *a)
{
    tcg_gen_shri_i32(cpu_regs[a->b], cpu_regs[a->b], a->u);
    if (a->f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, cpu_regs[a->b], 0);
        tcg_gen_shri_i32(cpu_nf, cpu_regs[a->b], 31);
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
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, pos);
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
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, pos);
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
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, pos);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}

static bool trans_LSR_CC(DisasContext *dc, arg_LSR_CC *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c],  31);
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
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_LSR_CC_U6(DisasContext *dc, arg_LSR_CC_U6 *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
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
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
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
        tcg_gen_sari_i32(cpu_nf, cpu_regs[a->b], 31);
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
        tcg_gen_sari_i32(cpu_nf, cpu_regs[a->b], 31);
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
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c],  31);
        tcg_gen_sar_i32(cpu_regs[a->b], cpu_regs[a->b], shift_amt);
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
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ASR_CC_U6(DisasContext *dc, arg_ASR_CC_U6 *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
        tcg_gen_sar_i32(cpu_regs[a->b], cpu_regs[a->b], shift_amt);
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
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
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
        tcg_gen_sari_i32(cpu_nf, cpu_regs[a->b], 31);
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
        tcg_gen_sari_i32(cpu_nf, cpu_regs[a->b], 31);
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
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, pos);
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
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, pos);
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
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, pos);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
    return true;
}


static bool trans_ROR_CC(DisasContext *dc, arg_ROR_CC *a)
{
if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_andi_i32(shift_amt, cpu_regs[a->c],  31);
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
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[a->b], cond, tcg_constant_i32(0), tmp, cpu_regs[a->b]);
    }
    return true;
}

static bool trans_ROR_CC_U6(DisasContext *dc, arg_ROR_CC_U6 *a)
{
    if (a->q == 0) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, cpu_regs[a->b]);
        TCGv_i32 shift_amt = tcg_temp_new_i32();
        tcg_gen_movi_i32(shift_amt, a->u & 31);
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
    } else if (a->q == 2) {
        TCGv_i32 cond = tcg_temp_new_i32();
        tcg_gen_mov_i32(cond, cpu_zf);
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
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_zf, cond, tcg_constant_i32(0), new_z, cpu_zf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_nf, cond, tcg_constant_i32(0), new_n, cpu_nf);
            tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, cond, tcg_constant_i32(0), new_c, cpu_cf);
        }
    }
    return true;
}

static bool trans_CMP(DisasContext *dc, arg_cmp *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, cpu_regs[a->b], cpu_regs[a->c]);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static void arc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    uint16_t insn_hi = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next, MO_LE);
    uint16_t op5 = (insn_hi >> 11) & 0x1F;

    if (op5 <= 0x0B) {
        uint16_t insn_lo = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next + 2, MO_LE);
        uint32_t insn = (insn_hi << 16) | insn_lo;
        if (!decode(dc, insn)) {
            gen_helper_halt(tcg_env);
            dc->base.is_jmp = DISAS_NORETURN;
         }
        dc->base.pc_next += 4;
    } else {
        if (!decode16(dc, insn_hi)) {
            gen_helper_halt(tcg_env);
            dc->base.is_jmp = DISAS_NORETURN;
        }
        dc->base.pc_next += 2;
    }
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