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
    bool has_delay_link;
    bool delay_is_cond;
    TCGv_i32 delay_cond_taken;
    CPUArchState *env;
    uint32_t lp_end;
    uint32_t lp_start_off;
} DisasContext;

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#include "decode-insns.c.inc"
#include "decode-insns16.c.inc"
#undef  HELPER_H

static TCGv_i32 cpu_pc;
static const int arc_reduced_regs[8] = {0, 1, 2, 3, 12, 13, 14, 15};
static TCGv_i32 cpu_regs[64];
static TCGv_i32 cpu_zf;
static TCGv_i32 cpu_nf;
static TCGv_i32 cpu_cf;
static TCGv_i32 cpu_vf;
static TCGv_i32 cpu_acch;
static TCGv_i32 cpu_accl;
static TCGv_i32 cpu_lp_end;
static TCGv_i32 cpu_lp_start;

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
    for (int i = 32; i < 64; i++)
    {
        char *name = g_strdup_printf("r%d", i);
        cpu_regs[i] = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, r[i]), name);
    }
    cpu_zf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, zf), "zf");
    cpu_nf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, nf), "nf");
    cpu_cf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, cf), "cf");
    cpu_vf = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, vf), "vf");
    cpu_acch = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, ACCH), "acch");
    cpu_accl = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, ACCL), "accl");
    cpu_regs[58] = cpu_accl;
    cpu_regs[59] = cpu_acch;
    cpu_lp_end = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, lp_end), "lp_end");
    cpu_lp_start = tcg_global_mem_new_i32(tcg_env, offsetof(CPUArcState, lp_start), "lp_start");

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

static void mov(TCGv_i32 s1, TCGv_i32 s2, int f)
{
    tcg_gen_mov_i32(s1, s2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, s1, 0);
        tcg_gen_shri_i32(cpu_nf, s1, 31);
    }
}

static void add(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    tcg_gen_add_i32(d, s1, s2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, d, orig_b);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, orig_c);
        tcg_gen_not_i32(vf_t0, vf_t0);
        tcg_gen_xor_i32(vf_t1, orig_b, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void add1(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, s2, 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    tcg_gen_add_i32(d, s1, shift_c);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, d, orig_b);
        TCGv_i32 vf_t0_8 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_8 = tcg_temp_new_i32();
        TCGv_i32 vf_res_8 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_8, orig_b, shift_c);
        tcg_gen_not_i32(vf_t0_8, vf_t0_8);
        tcg_gen_xor_i32(vf_t1_8, orig_b, d);
        tcg_gen_and_i32(vf_t0_8, vf_t0_8, vf_t1_8);
        tcg_gen_shri_i32(vf_res_8, vf_t0_8, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_8);
    }
}

static void add2(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, s2, 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    tcg_gen_add_i32(d, s1, shift_c);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf,d, orig_b);
        TCGv_i32 vf_t0_13 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_13 = tcg_temp_new_i32();
        TCGv_i32 vf_res_13 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_13, orig_b, shift_c);
        tcg_gen_not_i32(vf_t0_13, vf_t0_13);
        tcg_gen_xor_i32(vf_t1_13, orig_b, d);
        tcg_gen_and_i32(vf_t0_13, vf_t0_13, vf_t1_13);
        tcg_gen_shri_i32(vf_res_13, vf_t0_13, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_13);
    }
}

static void add3(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, s2, 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    tcg_gen_add_i32(d, s1, shift_c);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf,d, orig_b);
        TCGv_i32 vf_t0_13 = tcg_temp_new_i32();
        TCGv_i32 vf_t1_13 = tcg_temp_new_i32();
        TCGv_i32 vf_res_13 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0_13, orig_b, shift_c);
        tcg_gen_not_i32(vf_t0_13, vf_t0_13);
        tcg_gen_xor_i32(vf_t1_13, orig_b, d);
        tcg_gen_and_i32(vf_t0_13, vf_t0_13, vf_t1_13);
        tcg_gen_shri_i32(vf_res_13, vf_t0_13, 31);
        tcg_gen_mov_i32(cpu_vf, vf_res_13);
    }
}

static void mpy(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, s1, s2);
    tcg_gen_mov_i32(d, lo);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo, 0);
        tcg_gen_shri_i32(cpu_nf, hi, 31);
        {
            TCGv_i32 sign_ext = tcg_temp_new_i32();
            tcg_gen_sari_i32(sign_ext, lo, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, cpu_vf, hi, sign_ext);
        }
    }
}

static void sub(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    tcg_gen_sub_i32(d, s1, s2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, orig_c);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, orig_c);
        tcg_gen_xor_i32(vf_t1, orig_b, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void sub1(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, s2, 1);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    tcg_gen_sub_i32(d, s1, shift_c);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_c);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, shift_c);
        tcg_gen_xor_i32(vf_t1, orig_b, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void sub2(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, s2, 2);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    tcg_gen_sub_i32(d, s1, shift_c);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_c);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, shift_c);
        tcg_gen_xor_i32(vf_t1, orig_b, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void sub3(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 shift_c = tcg_temp_new_i32();
    tcg_gen_shli_i32(shift_c, s2, 3);
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    tcg_gen_sub_i32(d, s1, shift_c);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, shift_c);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, shift_c);
        tcg_gen_xor_i32(vf_t1, orig_b, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void and_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    tcg_gen_and_i32(d, s1, s2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void or_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    tcg_gen_or_i32(d, s1, s2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void xor_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    tcg_gen_xor_i32(d, s1, s2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void asl(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, s2, 31);
    tcg_gen_shl_i32(d, s1, shift_amt);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subfi_i32(inv_amt, 32, shift_amt);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
}

static void lsr(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, s2, 31);
    tcg_gen_shr_i32(d, s1, shift_amt);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_shr_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
}

static void asr(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, s2, 31);
    tcg_gen_sar_i32(d, s1, shift_amt);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        TCGv_i32 pos = tcg_temp_new_i32();
        tcg_gen_subi_i32(pos, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, pos);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
}

static void ror(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 shift_amt = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift_amt, s2, 31);
    tcg_gen_rotr_i32(d, s1, shift_amt);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        TCGv_i32 inv_amt = tcg_temp_new_i32();
        tcg_gen_subi_i32(inv_amt, shift_amt, 1);
        TCGv_i32 bit = tcg_temp_new_i32();
        tcg_gen_sar_i32(bit, orig_b, inv_amt);
        tcg_gen_andi_i32(bit, bit, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, cpu_cf, shift_amt, tcg_constant_i32(0), tcg_constant_i32(0), bit);
    }
}

static void asl_simple(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    tcg_gen_shli_i32(d, s, 1);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
}

static void lsr_simple(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    tcg_gen_shri_i32(d, s, 1);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_andi_i32(cpu_cf, orig_c, 1);
    }
}

static void asr_simple(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    tcg_gen_sari_i32(d, s, 1);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_andi_i32(cpu_cf, orig_c, 1);
    }
}

static void ror_simple(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    tcg_gen_rotri_i32(d, s, 1);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_andi_i32(cpu_cf, orig_c, 1);
    }
}

static void cmp(TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, s1, s2);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, s1, s2);
    TCGv_i32 vf_t0 = tcg_temp_new_i32();
    TCGv_i32 vf_t1 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0, s1, s2);
    tcg_gen_xor_i32(vf_t1, s1, tmp);
    tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
    tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
}

static void not_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_not_i32(d, s);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void aex(TCGv_i32 addr, TCGv_i32 b_reg)
{
    TCGv_i32 result = tcg_temp_new_i32();
    gen_helper_aex(result, tcg_env, addr, b_reg);
    tcg_gen_mov_i32(b_reg, result);
}

static void abs_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    tcg_gen_abs_i32(d, orig_c);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_nf, orig_c, 0x80000000);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_vf, orig_c, 0x80000000);
    }
}

static void adc(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_add2_i32(sum_lo, sum_hi, s1, tcg_constant_i32(0), s2, tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_add2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(d, result);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, orig_c);
        tcg_gen_not_i32(vf_t0, vf_t0);
        tcg_gen_xor_i32(vf_t1, orig_b, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void asr_fixed(TCGv_i32 d, TCGv_i32 s, int n, int f)
{
    tcg_gen_sari_i32(d, s, n);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void lsr_fixed(TCGv_i32 d, TCGv_i32 s, int n, int f)
{
    tcg_gen_shri_i32(d, s, n);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_movi_i32(cpu_nf, 0);
    }
}

static void lsl_fixed(TCGv_i32 d, TCGv_i32 s, int n, int f)
{
    tcg_gen_shli_i32(d, s, n);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void ror8_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_rotri_i32(d, s, 8);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void rol(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    tcg_gen_rotli_i32(d, s, 1);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
}

static void max_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    tcg_gen_smax_i32(d, s1, s2);
    if (f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_sub_i32(alu, orig_b, orig_c);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, orig_c, orig_b);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, orig_c);
        tcg_gen_xor_i32(vf_t1, orig_b, alu);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void min_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    tcg_gen_smin_i32(d, s1, s2);
    if (f) {
        TCGv_i32 alu = tcg_temp_new_i32();
        tcg_gen_sub_i32(alu, orig_b, orig_c);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, alu, 0);
        tcg_gen_shri_i32(cpu_nf, alu, 31);
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_cf, orig_b, orig_c);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, orig_c);
        tcg_gen_xor_i32(vf_t1, orig_b, alu);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void sexb_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_ext8s_i32(d, s);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void sexh_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_ext16s_i32(d, s);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void extb_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_ext8u_i32(d, s);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_mov_i32(cpu_nf, tcg_constant_i32(0));
    }
}

static void exth_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_ext16u_i32(d, s);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_mov_i32(cpu_nf, tcg_constant_i32(0));
    }
}

static void rcmp_op(TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_sub_i32(tmp, s2, s1);
    tcg_gen_setcond_i32(TCG_COND_EQ, cpu_zf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LT, cpu_nf, tmp, tcg_constant_i32(0));
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, s2, s1);
    TCGv_i32 vf_t0 = tcg_temp_new_i32();
    TCGv_i32 vf_t1 = tcg_temp_new_i32();
    tcg_gen_xor_i32(vf_t0, s2, s1);
    tcg_gen_xor_i32(vf_t1, s2, tmp);
    tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
    tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
}

static void sbc_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    TCGv_i32 sum_lo = tcg_temp_new_i32();
    TCGv_i32 sum_hi = tcg_temp_new_i32();
    tcg_gen_sub2_i32(sum_lo, sum_hi, s1, tcg_constant_i32(0), s2, tcg_constant_i32(0));
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 carry_out = tcg_temp_new_i32();
    tcg_gen_sub2_i32(result, carry_out, sum_lo, sum_hi, cpu_cf, tcg_constant_i32(0));
    tcg_gen_mov_i32(d, result);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_cf, carry_out, 0);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, orig_c);
        tcg_gen_xor_i32(vf_t1, orig_b, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void bic_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    tcg_gen_andc_i32(d, s1, s2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void tst_op(TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, s1, s2);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
}

static void btst_op(TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, s2, 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, s1, mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
}

static void bset_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, s2, 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(d, s1, mask);
    if (f) {
        tcg_gen_movi_i32(cpu_zf, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void bclr_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, s2, 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(d, s1, mask);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void bxor_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, s2, 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_xor_i32(d, s1, mask);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void bmsk_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, s2, 31);
    TCGv_i32 inv = tcg_temp_new_i32();
    tcg_gen_subfi_i32(inv, 31, bitpos);
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_shr_i32(mask, tcg_constant_i32(0xFFFFFFFF), inv);
    tcg_gen_and_i32(d, s1, mask);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void swap_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_rotli_i32(d, s, 16);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void swape_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_bswap32_i32(d, s);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void rlc_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    TCGv_i32 shifted = tcg_temp_new_i32();
    tcg_gen_shli_i32(shifted, orig_c, 1);
    tcg_gen_or_i32(d, shifted, cpu_cf);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
}

static void rrc_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s);
    TCGv_i32 shifted = tcg_temp_new_i32();
    tcg_gen_shri_i32(shifted, orig_c, 1);
    TCGv_i32 carry_hi = tcg_temp_new_i32();
    tcg_gen_shli_i32(carry_hi, cpu_cf, 31);
    tcg_gen_or_i32(d, shifted, carry_hi);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_shri_i32(cpu_cf, orig_c, 31);
    }
}

static void mpym_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_muls2_i32(lo, hi, s1, s2);
    tcg_gen_mov_i32(d, hi);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
}

static void mpymu_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, s1, s2);
    tcg_gen_mov_i32(d, hi);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
}

static void mpyu_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(lo, hi, s1, s2);
    tcg_gen_mov_i32(d, lo);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_setcondi_i32(TCG_COND_NE, cpu_vf, hi, 0);
    }
}

static void rsub_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    tcg_gen_sub_i32(d, s2, s1);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_c, orig_b);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_c, orig_b);
        tcg_gen_xor_i32(vf_t1, orig_c, d);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void set_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int i, int f)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 orig_c = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_c, s2);
    tcg_gen_setcond_i32(setcc_conds[i], d, s1, s2);
    if (f) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp, orig_b, orig_c);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
        tcg_gen_shri_i32(cpu_nf, tmp, 31);
        tcg_gen_setcond_i32(TCG_COND_LTU, cpu_cf, orig_b, orig_c);
        TCGv_i32 vf_t0 = tcg_temp_new_i32();
        TCGv_i32 vf_t1 = tcg_temp_new_i32();
        tcg_gen_xor_i32(vf_t0, orig_b, orig_c);
        tcg_gen_xor_i32(vf_t1, orig_b, tmp);
        tcg_gen_and_i32(vf_t0, vf_t0, vf_t1);
        tcg_gen_shri_i32(cpu_vf, vf_t0, 31);
    }
}

static void bmskn_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, s2, 31);
    TCGv_i32 inv = tcg_temp_new_i32();
    tcg_gen_subfi_i32(inv, 31, bitpos);
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_shr_i32(mask, tcg_constant_i32(0xFFFFFFFF), inv);
    tcg_gen_andc_i32(d, s1, mask);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void mpyw_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 lo1 = tcg_temp_new_i32();
    TCGv_i32 lo2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(lo1, s1);
    tcg_gen_ext16s_i32(lo2, s2);
    tcg_gen_mul_i32(d, lo1, lo2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void mpywu_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 lo1 = tcg_temp_new_i32();
    TCGv_i32 lo2 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(lo1, s1);
    tcg_gen_ext16u_i32(lo2, s2);
    tcg_gen_mul_i32(d, lo1, lo2);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
}

static void ffs_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_s = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_s, s);
    tcg_gen_ctzi_i32(d, s, 31);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, orig_s, 0);
        tcg_gen_shri_i32(cpu_nf, orig_s, 31);
    }
}

static void fls_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_s = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_s, s);
    TCGv_i32 clz_val = tcg_temp_new_i32();
    tcg_gen_clzi_i32(clz_val, s, 31);
    tcg_gen_subfi_i32(d, 31, clz_val);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, orig_s, 0);
        tcg_gen_shri_i32(cpu_nf, orig_s, 31);
    }
}

static void norm_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_s = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_s, s);
    TCGv_i32 sign_mask = tcg_temp_new_i32();
    tcg_gen_sari_i32(sign_mask, s, 31);
    TCGv_i32 flipped = tcg_temp_new_i32();
    tcg_gen_xor_i32(flipped, s, sign_mask);
    TCGv_i32 clz_val = tcg_temp_new_i32();
    tcg_gen_clzi_i32(clz_val, flipped, 32);
    tcg_gen_subi_i32(d, clz_val, 1);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, orig_s, 0);
        tcg_gen_shri_i32(cpu_nf, orig_s, 31);
    }
}

static void rol8_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    tcg_gen_rotli_i32(d, s, 8);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
        tcg_gen_shri_i32(cpu_nf, d, 31);
    }
}

static void normh_op(TCGv_i32 d, TCGv_i32 s, int f)
{
    TCGv_i32 orig_s = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_s, s);
    TCGv_i32 sext = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(sext, s);
    TCGv_i32 sign_mask = tcg_temp_new_i32();
    tcg_gen_sari_i32(sign_mask, sext, 31);
    TCGv_i32 flipped = tcg_temp_new_i32();
    tcg_gen_xor_i32(flipped, sext, sign_mask);
    TCGv_i32 clz_val = tcg_temp_new_i32();
    tcg_gen_clzi_i32(clz_val, flipped, 32);
    tcg_gen_subi_i32(d, clz_val, 17);
    if (f) {
        TCGv_i32 lo16 = tcg_temp_new_i32();
        tcg_gen_andi_i32(lo16, orig_s, 0xFFFF);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, lo16, 0);
        tcg_gen_extract_i32(cpu_nf, orig_s, 15, 1);
    }
}

static void flag_op(TCGv_i32 s)
{
    tcg_gen_extract_i32(cpu_vf, s, 8, 1);
    tcg_gen_extract_i32(cpu_cf, s, 9, 1);
    tcg_gen_extract_i32(cpu_nf, s, 10, 1);
    tcg_gen_extract_i32(cpu_zf, s, 11, 1);
}

static void lr_op(TCGv_i32 d, TCGv_i32 addr)
{
    gen_helper_lr(d, tcg_env, addr);
} 

static void sr_op(TCGv_i32 val, TCGv_i32 addr)
{
    gen_helper_sr(tcg_env, addr, val);
}

static void ld_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int x, int zz, int aa)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 addr = tcg_temp_new_i32();
    if (aa == 2) {
        tcg_gen_mov_i32(addr, orig_b);
    } else if (aa == 3) {
        int shift = (zz == 2) ? 1 : (zz == 1) ? 0 : 2;
        TCGv_i32 scaled_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(scaled_c, s2, shift);
        tcg_gen_add_i32(addr, orig_b, scaled_c);
    } else {
        tcg_gen_add_i32(addr, orig_b, s2);
    }
    MemOp mop;
    if (zz == 0) {
        mop = MO_LEUL;
    } else if (zz == 1) {
        mop = x ? MO_SB : MO_UB;
    } else if (zz == 2) {
        mop = x ? MO_LESW : MO_LEUW;
    } else {
        mop = MO_LEUL;
    }
    tcg_gen_qemu_ld_i32(d, addr, MMU_USER_IDX, mop);
    if (aa == 1) {
        tcg_gen_mov_i32(s1, addr);
    } else if (aa == 2) {
        tcg_gen_add_i32(s1, orig_b, s2);
    }
}

static void div_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 is_zero = tcg_temp_new_i32();
    tcg_gen_setcondi_i32(TCG_COND_EQ, is_zero, s2, 0);
    TCGv_i32 is_minint = tcg_temp_new_i32();
    tcg_gen_setcondi_i32(TCG_COND_EQ, is_minint, s1, 0x80000000);
    TCGv_i32 is_negone = tcg_temp_new_i32();
    tcg_gen_setcondi_i32(TCG_COND_EQ, is_negone, s2, -1);
    TCGv_i32 is_overflow = tcg_temp_new_i32();
    tcg_gen_and_i32(is_overflow, is_minint, is_negone);
    TCGv_i32 bad = tcg_temp_new_i32();
    tcg_gen_or_i32(bad, is_zero, is_overflow);
    TCGv_i32 safe_divisor = tcg_temp_new_i32();
    tcg_gen_movcond_i32(TCG_COND_NE, safe_divisor, bad, tcg_constant_i32(0), tcg_constant_i32(1), s2);
    TCGv_i32 quotient = tcg_temp_new_i32();
    tcg_gen_div_i32(quotient, s1, safe_divisor);
    tcg_gen_movcond_i32(TCG_COND_NE, d, bad, tcg_constant_i32(0), s1, quotient);
    if (f) {
        TCGv_i32 new_zf = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, new_zf, quotient, 0);
        tcg_gen_movcond_i32(TCG_COND_NE, cpu_zf, bad, tcg_constant_i32(0), cpu_zf, new_zf);
        TCGv_i32 new_nf = tcg_temp_new_i32();
        tcg_gen_shri_i32(new_nf, quotient, 31);
        tcg_gen_movcond_i32(TCG_COND_NE, cpu_nf, bad, tcg_constant_i32(0), cpu_nf, new_nf);  
        tcg_gen_mov_i32(cpu_vf, bad);
    }
}

static void divu_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 bad = tcg_temp_new_i32();
    tcg_gen_setcondi_i32(TCG_COND_EQ, bad, s2, 0);
    TCGv_i32 safe_divisor = tcg_temp_new_i32();
    tcg_gen_movcond_i32(TCG_COND_NE, safe_divisor, bad, tcg_constant_i32(0), tcg_constant_i32(1), s2);
    TCGv_i32 quotient = tcg_temp_new_i32();
    tcg_gen_divu_i32(quotient, s1, safe_divisor);
    tcg_gen_movcond_i32(TCG_COND_NE, d, bad, tcg_constant_i32(0), s1, quotient);
    if (f) {
        TCGv_i32 new_zf = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, new_zf, quotient, 0);
        tcg_gen_movcond_i32(TCG_COND_NE, cpu_zf, bad, tcg_constant_i32(0), cpu_zf, new_zf);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_mov_i32(cpu_vf, bad);
    }
}

static void remu_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 bad = tcg_temp_new_i32();
    tcg_gen_setcondi_i32(TCG_COND_EQ, bad, s2, 0);
    TCGv_i32 safe_divisor = tcg_temp_new_i32();
    tcg_gen_movcond_i32(TCG_COND_NE, safe_divisor, bad, tcg_constant_i32(0), tcg_constant_i32(1), s2);
    TCGv_i32 remainder = tcg_temp_new_i32();
    tcg_gen_remu_i32(remainder, s1, safe_divisor);
    tcg_gen_movcond_i32(TCG_COND_NE, d, bad, tcg_constant_i32(0), s1, remainder);
    if (f) {
        TCGv_i32 new_zf = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, new_zf, remainder, 0);
        tcg_gen_movcond_i32(TCG_COND_NE, cpu_zf, bad, tcg_constant_i32(0), cpu_zf, new_zf);
        tcg_gen_movi_i32(cpu_nf, 0);
        tcg_gen_mov_i32(cpu_vf, bad);
    }
}

static void mpyd_op(TCGv_i32 d_lo, TCGv_i32 d_hi, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    tcg_gen_muls2_i32(d_lo, d_hi, s1, s2);
    if (f) {
        TCGv_i32 zero_check = tcg_temp_new_i32();
        tcg_gen_or_i32(zero_check, d_lo, d_hi);
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, zero_check, 0);
        tcg_gen_shri_i32(cpu_nf, d_hi, 31);
        tcg_gen_movi_i32(cpu_vf, 0);
    }
}

static void mpydu_op(TCGv_i32 d_lo, TCGv_i32 d_hi, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    tcg_gen_mulu2_i32(d_lo, d_hi, s1, s2);
    if (f) {
        tcg_gen_movi_i32(cpu_vf, 0);
    }
}

static void dmach_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(b_lo, s1);
    tcg_gen_sari_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(c_lo, s2);
    tcg_gen_sari_i32(c_hi, s2, 16);
    TCGv_i32 prod_lo = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_lo, b_lo, c_lo);
    TCGv_i32 prod_hi = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_hi, b_hi, c_hi);
    TCGv_i32 sum = tcg_temp_new_i32();
    tcg_gen_add_i32(sum, prod_lo, prod_hi);
    TCGv_i64 sum64 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(sum64, sum);
    TCGv_i64 acc64 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(acc64, cpu_accl, cpu_acch);
    TCGv_i64 result64 = tcg_temp_new_i64();
    tcg_gen_add_i64(result64, acc64, sum64);
    tcg_gen_extr_i64_i32(cpu_accl, cpu_acch, result64);
    tcg_gen_mov_i32(d, cpu_accl);
}

static void dmachu_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(b_lo, s1);
    tcg_gen_shri_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(c_lo, s2);
    tcg_gen_shri_i32(c_hi, s2, 16);
    TCGv_i32 prod_lo = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_lo, b_lo, c_lo);
    TCGv_i32 prod_hi = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_hi, b_hi, c_hi);
    TCGv_i32 sum = tcg_temp_new_i32();
    tcg_gen_add_i32(sum, prod_lo, prod_hi);
    TCGv_i64 sum64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(sum64, sum);
    TCGv_i64 acc64 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(acc64, cpu_accl, cpu_acch);
    TCGv_i64 result64 = tcg_temp_new_i64();
    tcg_gen_add_i64(result64, acc64, sum64);
    tcg_gen_extr_i64_i32(cpu_accl, cpu_acch, result64);
    tcg_gen_mov_i32(d, cpu_accl);
}

static void vmpy2h_op(TCGv_i32 d_w0, TCGv_i32 d_w1, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(b_lo, s1);
    tcg_gen_sari_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(c_lo, s2);
    tcg_gen_sari_i32(c_hi, s2, 16);
    tcg_gen_mul_i32(d_w0, b_lo, c_lo);
    tcg_gen_mul_i32(d_w1, b_hi, c_hi);
    tcg_gen_mov_i32(cpu_accl, d_w0);
    tcg_gen_mov_i32(cpu_acch, d_w1);
}

static void vmpy2hu_op(TCGv_i32 d_w0, TCGv_i32 d_w1, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(b_lo, s1);
    tcg_gen_shri_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(c_lo, s2);
    tcg_gen_shri_i32(c_hi, s2, 16);
    tcg_gen_mul_i32(d_w0, b_lo, c_lo);
    tcg_gen_mul_i32(d_w1, b_hi, c_hi);
    tcg_gen_mov_i32(cpu_accl, d_w0);
    tcg_gen_mov_i32(cpu_acch, d_w1);
}

static void vsub2_op(TCGv_i32 d_w0, TCGv_i32 d_w1, TCGv_i32 s1_w0, TCGv_i32 s1_w1, TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    tcg_gen_sub_i32(d_w0, s1_w0, s2_w0);
    tcg_gen_sub_i32(d_w1, s1_w1, s2_w1);
}

static void vsub2h_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(b_lo, s1);
    tcg_gen_shri_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(c_lo, s2);
    tcg_gen_shri_i32(c_hi, s2, 16);
    TCGv_i32 r_lo = tcg_temp_new_i32();
    tcg_gen_sub_i32(r_lo, b_lo, c_lo);
    TCGv_i32 r_hi = tcg_temp_new_i32();
    tcg_gen_sub_i32(r_hi, b_hi, c_hi);
    tcg_gen_andi_i32(r_lo, r_lo, 0xffff);
    tcg_gen_deposit_i32(d, r_lo, r_hi, 16, 16);
}

static void vadd2_op(TCGv_i32 d_w0, TCGv_i32 d_w1,TCGv_i32 s1_w0, TCGv_i32 s1_w1,TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    tcg_gen_add_i32(d_w0, s1_w0, s2_w0);
    tcg_gen_add_i32(d_w1, s1_w1, s2_w1);
}

static void vadd2h_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(b_lo, s1);
    tcg_gen_shri_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(c_lo, s2);
    tcg_gen_shri_i32(c_hi, s2, 16);
    TCGv_i32 r_lo = tcg_temp_new_i32();
    tcg_gen_add_i32(r_lo, b_lo, c_lo);
    TCGv_i32 r_hi = tcg_temp_new_i32();
    tcg_gen_add_i32(r_hi, b_hi, c_hi);
    tcg_gen_andi_i32(r_lo, r_lo, 0xffff);
    tcg_gen_deposit_i32(d, r_lo, r_hi, 16, 16);
}

static void vadd4h_op(TCGv_i32 d_w0, TCGv_i32 d_w1,TCGv_i32 s1_w0, TCGv_i32 s1_w1, TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    vadd2h_op(d_w0, s1_w0, s2_w0);
    vadd2h_op(d_w1, s1_w1, s2_w1);
}

static void vaddsub_op(TCGv_i32 d_w0, TCGv_i32 d_w1,TCGv_i32 s1_w0, TCGv_i32 s1_w1, TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    tcg_gen_add_i32(d_w0, s1_w0, s2_w0);
    tcg_gen_sub_i32(d_w1, s1_w1, s2_w1);
}

static void vaddsub2h_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(b_lo, s1);
    tcg_gen_shri_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(c_lo, s2);
    tcg_gen_shri_i32(c_hi, s2, 16);
    TCGv_i32 r_lo = tcg_temp_new_i32();
    tcg_gen_add_i32(r_lo, b_lo, c_lo);
    TCGv_i32 r_hi = tcg_temp_new_i32();
    tcg_gen_sub_i32(r_hi, b_hi, c_hi);
    tcg_gen_andi_i32(r_lo, r_lo, 0xffff);
    tcg_gen_deposit_i32(d, r_lo, r_hi, 16, 16);
}

 static void vaddsub4h_op(TCGv_i32 d_w0, TCGv_i32 d_w1,TCGv_i32 s1_w0, TCGv_i32 s1_w1,TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    vaddsub2h_op(d_w0, s1_w0, s2_w0);
    vaddsub2h_op(d_w1, s1_w1, s2_w1);
}

static void vsubadd_op(TCGv_i32 d_w0, TCGv_i32 d_w1,TCGv_i32 s1_w0, TCGv_i32 s1_w1,TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    tcg_gen_sub_i32(d_w0, s1_w0, s2_w0);
    tcg_gen_add_i32(d_w1, s1_w1, s2_w1);
}

static void vsubadd2h_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(b_lo, s1);
    tcg_gen_shri_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(c_lo, s2);
    tcg_gen_shri_i32(c_hi, s2, 16);
    TCGv_i32 r_lo = tcg_temp_new_i32();
    tcg_gen_sub_i32(r_lo, b_lo, c_lo);
    TCGv_i32 r_hi = tcg_temp_new_i32();
    tcg_gen_add_i32(r_hi, b_hi, c_hi);
    tcg_gen_andi_i32(r_lo, r_lo, 0xffff);
    tcg_gen_deposit_i32(d, r_lo, r_hi, 16, 16);
}

static void vsubadd4h_op(TCGv_i32 d_w0, TCGv_i32 d_w1,TCGv_i32 s1_w0, TCGv_i32 s1_w1,TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    vsubadd2h_op(d_w0, s1_w0, s2_w0);
    vsubadd2h_op(d_w1, s1_w1, s2_w1);
}

static void vmac2h_op(TCGv_i32 d_w0, TCGv_i32 d_w1, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(b_lo, s1);
    tcg_gen_sari_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(c_lo, s2);
    tcg_gen_sari_i32(c_hi, s2, 16);
    TCGv_i32 prod_lo = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_lo, b_lo, c_lo);
    TCGv_i32 prod_hi = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_hi, b_hi, c_hi);
    tcg_gen_add_i32(cpu_accl, cpu_accl, prod_lo);
    tcg_gen_add_i32(cpu_acch, cpu_acch, prod_hi);
    tcg_gen_mov_i32(d_w0, cpu_accl);
    tcg_gen_mov_i32(d_w1, cpu_acch);
}

static void vmac2hu_op(TCGv_i32 d_w0, TCGv_i32 d_w1, TCGv_i32 s1, TCGv_i32 s2)
{
    TCGv_i32 b_lo = tcg_temp_new_i32();
    TCGv_i32 b_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(b_lo, s1);
    tcg_gen_shri_i32(b_hi, s1, 16);
    TCGv_i32 c_lo = tcg_temp_new_i32();
    TCGv_i32 c_hi = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(c_lo, s2);
    tcg_gen_shri_i32(c_hi, s2, 16);
    TCGv_i32 prod_lo = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_lo, b_lo, c_lo);
    TCGv_i32 prod_hi = tcg_temp_new_i32();
    tcg_gen_mul_i32(prod_hi, b_hi, c_hi);
    tcg_gen_add_i32(cpu_accl, cpu_accl, prod_lo);
    tcg_gen_add_i32(cpu_acch, cpu_acch, prod_hi);
    tcg_gen_mov_i32(d_w0, cpu_accl);
    tcg_gen_mov_i32(d_w1, cpu_acch);
}

static void vsub4h_op(TCGv_i32 d_w0, TCGv_i32 d_w1,TCGv_i32 s1_w0, TCGv_i32 s1_w1, TCGv_i32 s2_w0, TCGv_i32 s2_w1)
{
    vsub2h_op(d_w0, s1_w0, s2_w0);
    vsub2h_op(d_w1, s1_w1, s2_w1);
}

static void st_op(TCGv_i32 val, TCGv_i32 s1, TCGv_i32 s2, int zz, int aa)
{
    TCGv_i32 orig_b = tcg_temp_new_i32();
    tcg_gen_mov_i32(orig_b, s1);
    TCGv_i32 addr = tcg_temp_new_i32();
    if (aa == 2) {
        tcg_gen_mov_i32(addr, orig_b);
    } else if (aa == 3) {
        int shift = (zz == 2) ? 1 : (zz == 1) ? 0 : 2;
        TCGv_i32 scaled_c = tcg_temp_new_i32();
        tcg_gen_shli_i32(scaled_c, s2, shift);
        tcg_gen_add_i32(addr, orig_b, scaled_c);
    } else {
        tcg_gen_add_i32(addr, orig_b, s2);
    }
    MemOp mop;
    if (zz == 1) {
        mop = MO_UB;
    } else if (zz == 2) {
        mop = MO_LEUW;
    } else {
        mop = MO_LEUL;
    }   
    tcg_gen_qemu_st_i32(val, addr, MMU_USER_IDX, mop);
    if (aa == 1) {
        tcg_gen_mov_i32(s1, addr);
    } else if (aa == 2) {
        tcg_gen_add_i32(s1, orig_b, s2);
    }
}

static TCGv_i32 read_reg_or_limm(DisasContext *dc, int reg)
{
    if (reg == 62) {
        uint16_t hi = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 4, MO_LE);
        uint16_t lo = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 6, MO_LE);
        uint32_t limm = (hi << 16) | lo;
        dc->base.pc_next += 4;
        return tcg_constant_i32(limm);
    } else if (reg == 63) {
        return tcg_constant_i32(dc->base.pc_next & ~3);
    }

    return cpu_regs[reg];
}

static bool brcc_op(DisasContext *dc, uint32_t pc_base, uint32_t sbc, int n, TCGv_i32 src1, TCGv_i32 src2, TCGCond cond)
{
    uint32_t target = (pc_base & ~3) + (sbc << 1);
    if (n) {
        dc->has_delay_slot = true;
        dc->delay_is_cond = true;
        dc->delay_cond_taken = tcg_temp_new_i32();
        tcg_gen_setcond_i32(cond, dc->delay_cond_taken, src1, src2);
        dc->delay_target = tcg_constant_i32(target);
    } else {
        uint32_t fallthrough = dc->base.pc_next + 4;
        tcg_gen_movcond_i32(cond, cpu_pc, src1, src2, tcg_constant_i32(target), tcg_constant_i32(fallthrough));
        dc->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

static void xbfu_op(TCGv_i32 d, TCGv_i32 s1, TCGv_i32 s2, int f)
{
    TCGv_i32 n = tcg_temp_new_i32();
    tcg_gen_andi_i32(n, s2, 0x1F);
    TCGv_i32 m = tcg_temp_new_i32();
    tcg_gen_shri_i32(m, s2, 5);
    tcg_gen_andi_i32(m, m, 0x1F);
    tcg_gen_addi_i32(m, m, 1);
    TCGv_i32 shifted = tcg_temp_new_i32();
    tcg_gen_shr_i32(shifted, s1, n);
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), m);
    tcg_gen_subi_i32(mask, mask, 1);
    tcg_gen_movcond_i32(TCG_COND_EQ, mask, m, tcg_constant_i32(32), tcg_constant_i32(0xFFFFFFFF), mask);
    tcg_gen_and_i32(d, shifted, mask);
    if (f) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, d, 0);
    }
}

static bool trans_MOV(DisasContext *dc, arg_MOV *a)
{
    mov(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_MOV_U6(DisasContext *dc, arg_MOV_U6 *a)
{
    mov(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MOV_S12(DisasContext *dc, arg_MOV_S12 *a)
{
    mov(cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MOV_CC_F(DisasContext *dc, arg_MOV_CC_F *a)
{
    mov(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_MOV_CC_F_U6(DisasContext *dc, arg_MOV_CC_F_U6 *a)
{
    mov(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MOV_S_H_S3(DisasContext *dc, arg_MOV_S_H_S3 *a)
{
    tcg_gen_movi_i32(cpu_regs[a->h], a->s);
    return true;
}

static bool trans_MOV_S_NE(DisasContext *dc, arg_MOV_S_NE *a)
{
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[arc_reduced_regs[a->b]],cpu_zf, tcg_constant_i32(0),cpu_regs[a->h], read_reg_or_limm(dc, a->b));
    return true;
}

static bool trans_MOV_S_U8(DisasContext *dc, arg_MOV_S_U8 *a)
{
    tcg_gen_movi_i32(cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_FLAG(DisasContext *dc, arg_FLAG *a)
{
    flag_op(cpu_regs[a->c]);
    return true;
}

static bool trans_FLAG_U6(DisasContext *dc, arg_FLAG_U6 *a)
{
    flag_op(tcg_constant_i32(a->u));
    return true;
}

static bool trans_FLAG_S12(DisasContext *dc, arg_FLAG_S12 *a)
{
    flag_op(tcg_constant_i32(a->s));
    return true;
}

static bool trans_FLAG_CC(DisasContext *dc, arg_FLAG_CC *a)
{
    flag_op(cpu_regs[a->c]);
    return true;
}

static bool trans_FLAG_CC_U6(DisasContext *dc, arg_FLAG_CC_U6 *a)
{
    flag_op(tcg_constant_i32(a->u));
    return true;
}

static bool trans_ADD(DisasContext *dc, arg_ADD *a)
{
    TCGv_i32 s1 = read_reg_or_limm(dc, a->b);
    TCGv_i32 s2 = read_reg_or_limm(dc, a->c);
    add(cpu_regs[a->a], s1, s2, a->f);
    return true;
}

static bool trans_ADD_U6(DisasContext *dc, arg_ADD_U6 *a)
{
    add(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD_S12(DisasContext *dc, arg_ADD_S12 *a)
{
    add(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD_CC_F(DisasContext *dc, arg_ADD_CC_F *a)
{
    TCGv_i32 _rrl0_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl0_1 = read_reg_or_limm(dc, a->c);
    add(cpu_regs[a->b], _rrl0_0, _rrl0_1, a->f);
    return true;
}

static bool trans_ADD_CC_F_U6(DisasContext *dc, arg_ADD_CC_F_U6 *a)
{
    add(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD1(DisasContext *dc, arg_ADD1 *a)
{   
    TCGv_i32 _rrl1_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl1_1 = read_reg_or_limm(dc, a->c);
    add1(cpu_regs[a->a], _rrl1_0, _rrl1_1, a->f);
    return true;
}

static bool trans_ADD1_U6(DisasContext *dc, arg_ADD1_U6 *a)
{
    add1(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD1_S12(DisasContext *dc, arg_ADD1_S12 *a)
{
    add1(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD1_CC_F(DisasContext *dc, arg_ADD_CC_F *a)
{
    TCGv_i32 _rrl2_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl2_1 = read_reg_or_limm(dc, a->c);
    add1(cpu_regs[a->b], _rrl2_0,  _rrl2_1, a->f);
    return true;
}

static bool trans_ADD1_CC_F_U6(DisasContext *dc, arg_ADD1_CC_F_U6 *a)
{
    add1(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD2(DisasContext *dc, arg_ADD2 *a)
{   
    TCGv_i32 _rrl3_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl3_1 = read_reg_or_limm(dc, a->c);
    add2(cpu_regs[a->a],  _rrl3_0, _rrl3_1, a->f);
    return true;
}

static bool trans_ADD2_U6(DisasContext *dc, arg_ADD2_U6 *a)
{
    add2(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD2_S12(DisasContext *dc, arg_ADD2_S12 *a)
{
    add2(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD2_CC_F(DisasContext *dc, arg_ADD2_CC_F *a)
{
    TCGv_i32 _rrl4_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl4_1 = read_reg_or_limm(dc, a->c);
    add2(cpu_regs[a->b], _rrl4_0, _rrl4_1, a->f);
    return true;
}

static bool trans_ADD2_CC_F_U6(DisasContext *dc, arg_ADD2_CC_F_U6 *a)
{
    add2(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD3(DisasContext *dc, arg_ADD3 *a)
{   
    TCGv_i32 _rrl5_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl5_1 = read_reg_or_limm(dc, a->c);
    add3(cpu_regs[a->a], _rrl5_0, _rrl5_1, a->f);
    return true;
}

static bool trans_ADD3_U6(DisasContext *dc, arg_ADD3_U6 *a)
{
    add3(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD3_S12(DisasContext *dc, arg_ADD3_S12 *a)
{
    add3(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD3_CC_F(DisasContext *dc, arg_ADD3_CC_F *a)
{
    TCGv_i32 _rrl6_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl6_1 = read_reg_or_limm(dc, a->c);
    add3(cpu_regs[a->b], _rrl6_0, _rrl6_1, a->f);
    return true;
}

static bool trans_ADD3_CC_F_U6(DisasContext *dc, arg_ADD3_CC_F_U6 *a)
{
    add3(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD_S(DisasContext *dc, arg_ADD_S *a)
{
    tcg_gen_add_i32(cpu_regs[arc_reduced_regs[a->a]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ADD_S_R01(DisasContext *dc, arg_ADD_S_R01 *a)
{
    tcg_gen_addi_i32(cpu_regs[arc_reduced_regs[a->a]], cpu_regs[arc_reduced_regs[a->b]], a->u << 3);
    return true;
}

static bool trans_ADD_S_H(DisasContext *dc, arg_ADD_S_H *a)
{
    if (a->h == 30) {
        uint16_t limm_hi = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 2, MO_LE);
        uint16_t limm_lo = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 4, MO_LE);
        uint32_t limm = (limm_hi << 16) | limm_lo;
        tcg_gen_addi_i32(cpu_regs[arc_reduced_regs[a->b]], read_reg_or_limm(dc, arc_reduced_regs[a->b]), limm);
        dc->base.pc_next += 4;
    } else {
        tcg_gen_add_i32(cpu_regs[arc_reduced_regs[a->b]], read_reg_or_limm(dc, arc_reduced_regs[a->b]), cpu_regs[a->h]);
    }
    return true;
}

static bool trans_ADD_S_S3(DisasContext *dc, arg_ADD_S_S3 *a)
{
    int32_t imm = (a->s == 7) ? -1 : a->s;
    tcg_gen_addi_i32(cpu_regs[a->h], cpu_regs[a->h], imm);
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
    TCGv_i32 _rrl7_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl7_1 = read_reg_or_limm(dc, a->c);
    mpy(cpu_regs[a->a], _rrl7_0, _rrl7_1, a->f);
    return true;
}

static bool trans_MPY_u6(DisasContext *dc, arg_MPY_u6 *a)
{
    mpy(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPY_s12(DisasContext *dc, arg_MPY_s12 *a)
{
    mpy(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPY_CC_F(DisasContext *dc, arg_MPY_CC_F *a)
{
    TCGv_i32 _rrl8_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl8_1 = read_reg_or_limm(dc, a->c);
    mpy(cpu_regs[a->b], _rrl8_0, _rrl8_1, a->f);
    return true;
}

static bool trans_MPY_CC_F_U6(DisasContext *dc, arg_MPY_CC_F_U6 *a)
{
    mpy(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPY_S(DisasContext *dc, arg_MPY_S *a)
{
    tcg_gen_mul_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_SUB(DisasContext *dc, arg_SUB *a)
{
    TCGv_i32 _rrl9_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl9_1 = read_reg_or_limm(dc, a->c);
    sub(cpu_regs[a->a], _rrl9_0, _rrl9_1, a->f);
    return true;
}

static bool trans_SUB_u6(DisasContext *dc, arg_SUB_u6 *a)
{
    sub(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB_s12(DisasContext *dc, arg_SUB_s12 *a)
{
    sub(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB_CC(DisasContext *dc, arg_SUB_CC *a)
{
    TCGv_i32 _rrl10_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl10_1 = read_reg_or_limm(dc, a->c);
    sub(cpu_regs[a->b], _rrl10_0, _rrl10_1, a->f);
    return true;
}

static bool trans_SUB_CC_U6(DisasContext *dc, arg_SUB_CC_U6 *a)
{
    sub(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB1(DisasContext *dc, arg_SUB1 *a)
{
    TCGv_i32 _rrl11_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl11_1 = read_reg_or_limm(dc, a->c);
    sub1(cpu_regs[a->a], _rrl11_0, _rrl11_1, a->f);
    return true;
}

static bool trans_SUB1_U6(DisasContext *dc, arg_SUB1_U6 *a)
{
    sub1(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB1_S12(DisasContext *dc, arg_SUB1_S12 *a)
{
    sub1(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB1_CC(DisasContext *dc, arg_SUB1_CC *a)
{
    TCGv_i32 _rrl12_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl12_1 = read_reg_or_limm(dc, a->c);
    sub1(cpu_regs[a->b], _rrl12_0, _rrl12_1, a->f);
    return true;
}

static bool trans_SUB1_CC_U6(DisasContext *dc, arg_SUB1_CC_U6 *a)
{
    sub1(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB2(DisasContext *dc, arg_SUB2 *a)
{
    TCGv_i32 _rrl13_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl13_1 = read_reg_or_limm(dc, a->c);
    sub2(cpu_regs[a->a], _rrl13_0, _rrl13_1, a->f);
    return true;
}

static bool trans_SUB2_U6(DisasContext *dc, arg_SUB2_U6 *a)
{
    sub2(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB2_S12(DisasContext *dc, arg_SUB2_S12 *a)
{
    sub2(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB2_CC(DisasContext *dc, arg_SUB2_CC *a)
{
    TCGv_i32 _rrl14_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl14_1 = read_reg_or_limm(dc, a->c);
    sub2(cpu_regs[a->b], _rrl14_0, _rrl14_1, a->f);
    return true;
}

static bool trans_SUB2_CC_U6(DisasContext *dc, arg_SUB2_CC_U6 *a)
{
    sub2(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB3(DisasContext *dc, arg_SUB3 *a)
{
    TCGv_i32 _rrl15_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl15_1 = read_reg_or_limm(dc, a->c);
    sub3(cpu_regs[a->a], _rrl15_0, _rrl15_1, a->f);
    return true;
}

static bool trans_SUB3_U6(DisasContext *dc, arg_SUB3_U6 *a)
{
    sub3(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB3_S12(DisasContext *dc, arg_SUB3_S12 *a)
{
    sub3(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB3_CC(DisasContext *dc, arg_SUB3_CC *a)
{
    TCGv_i32 _rrl16_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl16_1 = read_reg_or_limm(dc, a->c);
    sub3(cpu_regs[a->b], _rrl16_0, _rrl16_1, a->f);
    return true;
}

static bool trans_SUB3_CC_U6(DisasContext *dc, arg_SUB3_CC_U6 *a)
{
    sub3(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
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
    TCGv_i32 _rrl17_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl17_1 = read_reg_or_limm(dc, a->c);
    and_op(cpu_regs[a->a], _rrl17_0, _rrl17_1, a->f);
    return true;
}

static bool trans_AND_U6(DisasContext *dc, arg_AND_U6 *a)
{
    and_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_AND_S12(DisasContext *dc, arg_AND_S12 *a)
{
    and_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_AND_CC(DisasContext *dc, arg_AND_CC *a)
{
    TCGv_i32 _rrl18_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl18_1 = read_reg_or_limm(dc, a->c);
    and_op(cpu_regs[a->b], _rrl18_0, _rrl18_1, a->f);
    return true;
}

static bool trans_AND_CC_U6(DisasContext *dc, arg_AND_CC_U6 *a)
{
    and_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_AND_S(DisasContext *dc, arg_AND_S *a)
{
    tcg_gen_and_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_OR(DisasContext *dc, arg_OR *a)
{
    TCGv_i32 _rrl19_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl19_1 = read_reg_or_limm(dc, a->c);
    or_op(cpu_regs[a->a], _rrl19_0, _rrl19_1, a->f);
    return true;
}

static bool trans_OR_U6(DisasContext *dc, arg_OR_U6 *a)
{
    or_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_OR_S12(DisasContext *dc, arg_OR_S12 *a)
{
    or_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_OR_CC(DisasContext *dc, arg_OR_CC *a)
{
    TCGv_i32 _rrl20_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl20_1 = read_reg_or_limm(dc, a->c);
    or_op(cpu_regs[a->b], _rrl20_0, _rrl20_1, a->f);
    return true;
}

static bool trans_OR_CC_U6(DisasContext *dc, arg_OR_CC_U6 *a)
{
    or_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_OR_S(DisasContext *dc, arg_OR_S *a)
{
    tcg_gen_or_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_XOR(DisasContext *dc, arg_XOR *a)
{
    TCGv_i32 _rrl21_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl21_1 = read_reg_or_limm(dc, a->c);
    xor_op(cpu_regs[a->a], _rrl21_0, _rrl21_1, a->f);
    return true;
}

static bool trans_XOR_U6(DisasContext *dc, arg_XOR_U6 *a)
{
    xor_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_XOR_S12(DisasContext *dc, arg_XOR_S12 *a)
{
    xor_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_XOR_CC(DisasContext *dc, arg_XOR_CC *a)
{
    TCGv_i32 _rrl22_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl22_1 = read_reg_or_limm(dc, a->c);
    xor_op(cpu_regs[a->b], _rrl22_0, _rrl22_1, a->f);
    return true;
}

static bool trans_XOR_CC_U6(DisasContext *dc, arg_XOR_CC_U6 *a)
{
    xor_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_XOR_S(DisasContext *dc, arg_XOR_S *a)
{
    tcg_gen_xor_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ASL(DisasContext *dc, arg_ASL *a)
{
    asl_simple(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_ASL_U6(DisasContext *dc, arg_ASL_U6 *a)
{
    asl_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASL_F(DisasContext *dc, arg_ASL_F *a)
{
    TCGv_i32 _rrl23_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl23_1 = read_reg_or_limm(dc, a->c);
    asl(cpu_regs[a->a], _rrl23_0, _rrl23_1, a->f);
    return true;
}

static bool trans_ASL_U6_F(DisasContext *dc, arg_ASL_U6_F *a)
{
    asl(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASL_S12(DisasContext *dc, arg_ASL_S12 *a)
{
    asl(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ASL_CC(DisasContext *dc, arg_ASL_CC *a)
{
    TCGv_i32 _rrl24_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl24_1 = read_reg_or_limm(dc, a->c);
    asl(cpu_regs[a->b], _rrl24_0, _rrl24_1, a->f);
    return true;
}

static bool trans_ASL_CC_U6(DisasContext *dc, arg_ASL_CC_U6 *a)
{
    asl(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
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
    tcg_gen_shli_i32(cpu_regs[arc_reduced_regs[a->c]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ASL_S_U5(DisasContext *dc, arg_ASL_S_U5 *a)
{
    tcg_gen_shli_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_LSR(DisasContext *dc, arg_LSR *a)
{
    lsr_simple(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_LSR_U6(DisasContext *dc, arg_LSR_U6 *a)
{
    lsr_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_LSR_F(DisasContext *dc, arg_LSR_F *a)
{
    TCGv_i32 _rrl25_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl25_1 = read_reg_or_limm(dc, a->c);
    lsr(cpu_regs[a->a], _rrl25_0, _rrl25_1, a->f);
    return true;
}

static bool trans_LSR_U6_F(DisasContext *dc, arg_LSR_U6_F *a)
{
    lsr(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_LSR_S12(DisasContext *dc, arg_LSR_S12 *a)
{
    lsr(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_LSR_CC(DisasContext *dc, arg_LSR_CC *a)
{
    TCGv_i32 _rrl26_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl26_1 = read_reg_or_limm(dc, a->c);
    lsr(cpu_regs[a->b], _rrl26_0, _rrl26_1, a->f);
    return true;
}

static bool trans_LSR_CC_U6(DisasContext *dc, arg_LSR_CC_U6 *a)
{
    lsr(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
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
    asr_simple(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_ASR_U6(DisasContext *dc, arg_ASR_U6 *a)
{
    asr_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASR_F(DisasContext *dc, arg_ASR_F *a)
{
    TCGv_i32 _rrl27_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl27_1 = read_reg_or_limm(dc, a->c);
    asr(cpu_regs[a->a], _rrl27_0, _rrl27_1, a->f);
    return true;
}

static bool trans_ASR_U6_F(DisasContext *dc, arg_ASR_U6_F *a)
{
    asr(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASR_S12(DisasContext *dc, arg_ASR_S12 *a)
{
    asr(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ASR_S_U3(DisasContext *dc, arg_ASR_S_U3 *a)
{
    tcg_gen_sari_i32(cpu_regs[arc_reduced_regs[a->c]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ASR_CC(DisasContext *dc, arg_ASR_CC *a)
{
    TCGv_i32 _rrl28_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl28_1 = read_reg_or_limm(dc, a->c);
    asr(cpu_regs[a->b], _rrl28_0, _rrl28_1, a->f);
    return true;
}

static bool trans_ASR_CC_U6(DisasContext *dc, arg_ASR_CC_U6 *a)
{
    asr(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
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
    ror_simple(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_ROR_U6(DisasContext *dc, arg_ROR_U6 *a)
{
    ror_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ROR_F(DisasContext *dc, arg_ROR_F *a)
{
    TCGv_i32 _rrl29_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl29_1 = read_reg_or_limm(dc, a->c);
    ror(cpu_regs[a->a], _rrl29_0, _rrl29_1, a->f);
    return true;
}

static bool trans_ROR_U6_F(DisasContext *dc, arg_ROR_U6_F *a)
{
    ror(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ROR_S12(DisasContext *dc, arg_ROR_S12 *a)
{
    ror(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ROR_CC(DisasContext *dc, arg_ROR_CC *a)
{
    TCGv_i32 _rrl30_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl30_1 = read_reg_or_limm(dc, a->c);
    ror(cpu_regs[a->b], _rrl30_0, _rrl30_1, a->f);
    return true;
}

static bool trans_ROR_CC_U6(DisasContext *dc, arg_ROR_CC_U6 *a)
{
    ror(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_CMP(DisasContext *dc, arg_CMP *a)
{
    cmp(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_CMP_U6(DisasContext *dc, arg_CMP_U6 *a)
{
    cmp(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_CMP_S12(DisasContext *dc, arg_CMP_S12 *a)
{
    cmp(cpu_regs[a->b], tcg_constant_i32(a->s));
    return true;
}

static bool trans_CMP_CC(DisasContext *dc, arg_CMP_CC *a)
{
    cmp(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_CMP_CC_U6(DisasContext *dc, arg_CMP_CC_U6 *a)
{
    cmp(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_CMP_S(DisasContext *dc, arg_CMP_S *a)
{
    if (a->h == 30) {
        uint16_t limm_hi = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 2, MO_LE);
        uint16_t limm_lo = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 4, MO_LE);
        uint32_t limm = (limm_hi << 16) | limm_lo;
        cmp(cpu_regs[arc_reduced_regs[a->b]], tcg_constant_i32(limm));
        dc->base.pc_next += 4;
    } else {
        cmp(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[a->h]);
    }
    return true;
}

static bool trans_CMP_S3(DisasContext *dc, arg_CMP_S3 *a)
{
    int32_t imm = (a->s == 7) ? -1 : a->s;
    if (a->h == 30) {
        uint16_t limm_hi = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 2, MO_LE);
        uint16_t limm_lo = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 4, MO_LE);
        uint32_t limm = (limm_hi << 16) | limm_lo;
        cmp(tcg_constant_i32(limm), tcg_constant_i32(imm));
        dc->base.pc_next += 4;
    } else {
        cmp(cpu_regs[a->h], tcg_constant_i32(imm));
    }
    return true;
}

static bool trans_CMP_S_U7(DisasContext *dc, arg_cmp_s_u7 *a)
{
    cmp(cpu_regs[arc_reduced_regs[a->b]], tcg_constant_i32(a->u7));
    return true;
}

static bool trans_BRANCH(DisasContext *dc, arg_BRANCH *a)
{
    uint32_t target = (dc->base.pc_next & ~3) + (a->sb << 1);
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
    uint32_t target = (dc->base.pc_next & ~3) + (a->sb * 2);
    tcg_gen_movi_i32(cpu_pc, target);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_BEQ_S(DisasContext *dc, arg_BEQ_S *a)
{
    uint32_t target = (dc->base.pc_next & ~3) + (a->sb * 2);
    uint32_t fallthrough = dc->base.pc_next + 2;
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_pc, cpu_zf, tcg_constant_i32(1), tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_BNE_S(DisasContext *dc, arg_BNE_S *a)
{
    uint32_t target = (dc->base.pc_next & ~3) + (a->sb * 2);
    uint32_t fallthrough = dc->base.pc_next + 2;
    tcg_gen_movcond_i32(TCG_COND_NE, cpu_pc, cpu_zf, tcg_constant_i32(1), tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool bcc_s_op(DisasContext *dc, int condcode, uint32_t sb)
{
    TCGv_i32 cond = gen_cc_test(condcode);
    uint32_t target = (dc->base.pc_next & ~3) + (sb * 2);
    uint32_t fallthrough = dc->base.pc_next + 2;
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_pc, cond, tcg_constant_i32(1), tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_BGT_S(DisasContext *dc, arg_BGT_S *a) { return bcc_s_op(dc, 9, a->s); }
static bool trans_BGE_S(DisasContext *dc, arg_BGE_S *a) { return bcc_s_op(dc, 10, a->s); }
static bool trans_BLT_S(DisasContext *dc, arg_BLT_S *a) { return bcc_s_op(dc, 11, a->s); }
static bool trans_BLE_S(DisasContext *dc, arg_BLE_S *a) { return bcc_s_op(dc, 12, a->s); }
static bool trans_BHI_S(DisasContext *dc, arg_BHI_S *a) { return bcc_s_op(dc, 13, a->s); }
static bool trans_BHS_S(DisasContext *dc, arg_BHS_S *a) { return bcc_s_op(dc, 6, a->s); }
static bool trans_BLO_S(DisasContext *dc, arg_BLO_S *a) { return bcc_s_op(dc, 5, a->s); }
static bool trans_BLS_S(DisasContext *dc, arg_BLS_S *a) { return bcc_s_op(dc, 14, a->s); }

static bool trans_BRANCH_C(DisasContext *dc, arg_BRANCH_C *a)
{
    uint32_t target = (dc->base.pc_next & ~3) + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, read_reg_or_limm(dc, a->c), 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, read_reg_or_limm(dc, a->b), bitpos);
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
    uint32_t target = (dc->base.pc_next & ~3) + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, read_reg_or_limm(dc, a->b), bitpos);
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
    uint32_t target = (dc->base.pc_next & ~3) + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, read_reg_or_limm(dc, a->c), 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, read_reg_or_limm(dc, a->b), bitpos);
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
    uint32_t target = (dc->base.pc_next & ~3) + (a->sbc << 1);
    uint32_t fallthrough = dc->base.pc_next +4;
    TCGv_i32 bitpos = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    TCGv_i32 bit = tcg_temp_new_i32();
    tcg_gen_shr_i32(bit, read_reg_or_limm(dc, a->b), bitpos);
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
    uint32_t target = (dc->base.pc_next & ~3) + (a->sb << 1);
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

static bool trans_BREQ_C(DisasContext *dc, arg_BREQ_C *a)
{
    uint32_t pc_base = dc->base.pc_next;
    TCGv_i32 _rrl31_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl31_1 = read_reg_or_limm(dc, a->c);
    return brcc_op(dc, pc_base, a->sbc, a->n, _rrl31_0, _rrl31_1, TCG_COND_EQ);
}

static bool trans_BREQ_C_U6(DisasContext *dc, arg_BREQ_C_U6 *a)
{
    uint32_t pc_base = dc->base.pc_next;
    return brcc_op(dc, pc_base, a->sbc, a->n, read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), TCG_COND_EQ);
}

static bool trans_BRNE_C(DisasContext *dc, arg_BRNE_C *a)
{
    uint32_t pc_base = dc->base.pc_next;
    TCGv_i32 _rrl32_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl32_1 = read_reg_or_limm(dc, a->c);
    return brcc_op(dc, pc_base, a->sbc, a->n, _rrl32_0, _rrl32_1, TCG_COND_NE);
}

static bool trans_BRNE_C_U6(DisasContext *dc, arg_BRNE_C_U6 *a)
{
    uint32_t pc_base = dc->base.pc_next;
    return brcc_op(dc, pc_base, a->sbc, a->n, read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), TCG_COND_NE);
}

static bool trans_BRLT_C(DisasContext *dc, arg_BRLT_C *a)
{
    uint32_t pc_base = dc->base.pc_next;
    TCGv_i32 _rrl33_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl33_1 = read_reg_or_limm(dc, a->c);
    return brcc_op(dc, pc_base, a->sbc, a->n, _rrl33_0, _rrl33_1, TCG_COND_LT);
}

static bool trans_BRLT_C_U6(DisasContext *dc, arg_BRLT_C_U6 *a)
{
    uint32_t pc_base = dc->base.pc_next;
    return brcc_op(dc, pc_base, a->sbc, a->n, read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), TCG_COND_LT);
}

static bool trans_BRGE_C(DisasContext *dc, arg_BRGE_C *a)
{
    uint32_t pc_base = dc->base.pc_next;
    TCGv_i32 _rrl34_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl34_1 = read_reg_or_limm(dc, a->c);
    return brcc_op(dc, pc_base, a->sbc, a->n, _rrl34_0, _rrl34_1, TCG_COND_GE);
}

static bool trans_BRGE_C_U6(DisasContext *dc, arg_BRGE_C_U6 *a)
{
    uint32_t pc_base = dc->base.pc_next;
    return brcc_op(dc, pc_base, a->sbc, a->n, read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), TCG_COND_GE);
}

static bool trans_BRLO_C(DisasContext *dc, arg_BRLO_C *a)
{
    uint32_t pc_base = dc->base.pc_next;
    TCGv_i32 _rrl35_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl35_1 = read_reg_or_limm(dc, a->c);
    return brcc_op(dc, pc_base, a->sbc, a->n, _rrl35_0, _rrl35_1, TCG_COND_LTU);
}

static bool trans_BRLO_C_U6(DisasContext *dc, arg_BRLO_C_U6 *a)
{
    uint32_t pc_base = dc->base.pc_next;
    return brcc_op(dc, pc_base, a->sbc, a->n, read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), TCG_COND_LTU);
}

static bool trans_BRHS_C(DisasContext *dc, arg_BRHS_C *a)
{
    uint32_t pc_base = dc->base.pc_next;
    TCGv_i32 _rrl36_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl36_1 = read_reg_or_limm(dc, a->c);
    return brcc_op(dc, pc_base, a->sbc, a->n, _rrl36_0, _rrl36_1, TCG_COND_GEU);
}

static bool trans_BRHS_C_U6(DisasContext *dc, arg_BRHS_C_U6 *a)
{
    uint32_t pc_base = dc->base.pc_next;
    return brcc_op(dc, pc_base, a->sbc, a->n, read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), TCG_COND_GEU);
}

static bool trans_JCC(DisasContext *dc, arg_JCC *a)
{
    tcg_gen_mov_i32(cpu_pc, read_reg_or_limm(dc, a->c));
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
    tcg_gen_mov_i32(dc->delay_target, read_reg_or_limm(dc, a->c));
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
    not_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_NOT_U6(DisasContext *dc, arg_NOT_U6 *a)
{
    not_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_NOT_S(DisasContext *dc, arg_NOT_S *a)
{
    tcg_gen_not_i32(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
} 

static bool trans_AEX(DisasContext *dc, arg_AEX *a)
{
    aex(cpu_regs[a->c], read_reg_or_limm(dc, a->b));
    return true;
}

static bool trans_AEX_U6(DisasContext *dc, arg_AEX_U6 *a)
{
    aex(tcg_constant_i32(a->u), read_reg_or_limm(dc, a->b));
    return true;
}

static bool trans_AEX_S12(DisasContext *dc, arg_AEX_S12 *a)
{
    aex(tcg_constant_i32(a->s), read_reg_or_limm(dc, a->b));
    return true;
}

static bool trans_AEX_CC(DisasContext *dc, arg_AEX_CC *a)
{
    aex(cpu_regs[a->c], read_reg_or_limm(dc, a->b));
    return true;
}

static bool trans_AEX_CC_U6(DisasContext *dc, arg_AEX_CC_U6 *a)
{
    aex(tcg_constant_i32(a->u), read_reg_or_limm(dc, a->b));
    return true;
}

static bool trans_ABS(DisasContext *dc, arg_ABS *a)
{
    abs_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_ABS_U6(DisasContext *dc, arg_ABS_U6 *a)
{
    abs_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ABS_S(DisasContext *dc, arg_ABS_S *a)
{
    tcg_gen_abs_i32(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_ADC(DisasContext *dc, arg_ADC *a)
{
    TCGv_i32 _rrl37_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl37_1 = read_reg_or_limm(dc, a->c);
    adc(cpu_regs[a->a], _rrl37_0, _rrl37_1, a->f);
    return true;
}

static bool trans_ADC_U6(DisasContext *dc, arg_ADC_U6 *a)
{
    adc(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADC_S12(DisasContext *dc, arg_ADC_S12 *a)
{
    adc(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADC_CC(DisasContext *dc, arg_ADC_CC *a)
{
    TCGv_i32 _rrl38_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl38_1 = read_reg_or_limm(dc, a->c);
    adc(cpu_regs[a->b], _rrl38_0, _rrl38_1, a->f);
    return true;
}

static bool trans_ADC_CC_U6(DisasContext *dc, arg_ADC_CC_U6 *a)
{
    adc(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASR16(DisasContext *dc, arg_ASR16 *a)
{
    asr_fixed(cpu_regs[a->b], read_reg_or_limm(dc, a->c), 16, a->f);
    return true;
}

static bool trans_ASR16_U6(DisasContext *dc, arg_ASR16_U6 *a)
{
    asr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 16, a->f);
    return true;
}

static bool trans_ASR8(DisasContext *dc, arg_ASR8 *a)
{
    asr_fixed(cpu_regs[a->b], read_reg_or_limm(dc, a->c), 8, a->f);
    return true;
}

static bool trans_ASR8_U6(DisasContext *dc, arg_ASR8_U6 *a)
{
    asr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 8, a->f);
    return true;
}

static bool trans_LSR16(DisasContext *dc, arg_LSR16 *a)
{
    lsr_fixed(cpu_regs[a->b], read_reg_or_limm(dc, a->c), 16, a->f);
    return true;
}

static bool trans_LSR16_U6(DisasContext *dc, arg_LSR16_U6 *a)
{
    lsr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 16, a->f);
    return true;
}

static bool trans_LSR8(DisasContext *dc, arg_LSR8 *a)
{
    lsr_fixed(cpu_regs[a->b], read_reg_or_limm(dc, a->c), 8, a->f);
    return true;
}

static bool trans_LSR8_U6(DisasContext *dc, arg_LSR8_U6 *a)
{
    lsr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 8, a->f);
    return true;
}

static bool trans_LSL16(DisasContext *dc, arg_LSL16 *a)
{
    lsl_fixed(cpu_regs[a->b], read_reg_or_limm(dc, a->c), 16, a->f);
    return true;
}

static bool trans_LSL16_U6(DisasContext *dc, arg_LSL16_U6 *a)
{
    lsl_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 16, a->f);
    return true;
}

static bool trans_LSL8(DisasContext *dc, arg_LSL8 *a)
{
    lsl_fixed(cpu_regs[a->b], read_reg_or_limm(dc, a->c), 8, a->f);
    return true;
}

static bool trans_LSL8_U6(DisasContext *dc, arg_LSL8_U6 *a)
{
    lsl_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 8, a->f);
    return true;
}

static bool trans_ROR8(DisasContext *dc, arg_ROR8 *a)
{
    ror8_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_ROR8_U6(DisasContext *dc, arg_ROR8_U6 *a)
{
    ror8_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ROL(DisasContext *dc, arg_ROL *a)
{
    rol(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_ROL_U6(DisasContext *dc, arg_ROL_U6 *a)
{
    rol(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MAX(DisasContext *dc, arg_MAX *a)
{
    TCGv_i32 _rrl39_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl39_1 = read_reg_or_limm(dc, a->c);
    max_op(cpu_regs[a->a], _rrl39_0, _rrl39_1, a->f);
    return true;
}

static bool trans_MAX_U6(DisasContext *dc, arg_MAX_U6 *a)
{
    max_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MAX_S12(DisasContext *dc, arg_MAX_S12 *a)
{
    max_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MAX_CC(DisasContext *dc, arg_MAX_CC *a)
{
    TCGv_i32 _rrl40_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl40_1 = read_reg_or_limm(dc, a->c);
    max_op(cpu_regs[a->b], _rrl40_0, _rrl40_1, a->f);
    return true;
}

static bool trans_MAX_CC_U6(DisasContext *dc, arg_MAX_CC_U6 *a)
{
    max_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MIN(DisasContext *dc, arg_MIN *a)
{
    TCGv_i32 _rrl41_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl41_1 = read_reg_or_limm(dc, a->c);
    min_op(cpu_regs[a->a], _rrl41_0, _rrl41_1, a->f);
    return true;
}

static bool trans_MIN_U6(DisasContext *dc, arg_MIN_U6 *a)
{
    min_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MIN_S12(DisasContext *dc, arg_MIN_S12 *a)
{
    min_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MIN_CC(DisasContext *dc, arg_MIN_CC *a)
{
    TCGv_i32 _rrl42_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl42_1 = read_reg_or_limm(dc, a->c);
    min_op(cpu_regs[a->b], _rrl42_0, _rrl42_1, a->f);
    return true;
}

static bool trans_MIN_CC_U6(DisasContext *dc, arg_MIN_CC_U6 *a)
{
    min_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SEXB(DisasContext *dc, arg_SEXB *a)
{
    sexb_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_SEXB_U6(DisasContext *dc, arg_SEXB_U6 *a)
{
    sexb_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SEXB_S(DisasContext *dc, arg_SEXB_S *a)
{
    tcg_gen_ext8s_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_SEXH(DisasContext *dc, arg_SEXH *a)
{
    sexh_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_SEXH_U6(DisasContext *dc, arg_SEXH_U6 *a)
{
    sexh_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SEXH_S(DisasContext *dc, arg_SEXH_S *a)
{
    tcg_gen_ext16s_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_EXTB(DisasContext *dc, arg_EXTB *a)
{
    extb_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_EXTB_U6(DisasContext *dc, arg_EXTB_U6 *a)
{
    extb_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_EXTB_S(DisasContext *dc, arg_EXTB_S *a)
{
    tcg_gen_ext8u_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_EXTH(DisasContext *dc, arg_EXTH *a)
{
    exth_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_EXTH_U6(DisasContext *dc, arg_EXTH_U6 *a)
{
    exth_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_EXTH_S(DisasContext *dc, arg_EXTH_S *a)
{
    tcg_gen_ext16u_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_RCMP(DisasContext *dc, arg_rcmp *a)
{
    rcmp_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_RCMP_U6(DisasContext *dc, arg_rcmp_u6 *a)
{
    rcmp_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_RCMP_S12(DisasContext *dc, arg_rcmp_s12 *a)
{
    rcmp_op(cpu_regs[a->b], tcg_constant_i32(a->s));
    return true;
}

static bool trans_RCMP_CC(DisasContext *dc, arg_rcmp_cc *a)
{
    rcmp_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_RCMP_CC_U6(DisasContext *dc, arg_rcmp_cc_u6 *a)
{
    rcmp_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_SBC(DisasContext *dc, arg_SBC *a)
{
    TCGv_i32 _rrl43_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl43_1 = read_reg_or_limm(dc, a->c);
    sbc_op(cpu_regs[a->a], _rrl43_0, _rrl43_1, a->f);
    return true;
}

static bool trans_SBC_U6(DisasContext *dc, arg_SBC_U6 *a)
{
    sbc_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SBC_S12(DisasContext *dc, arg_SBC_S12 *a)
{
    sbc_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SBC_CC(DisasContext *dc, arg_SBC_CC *a)
{
    TCGv_i32 _rrl44_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl44_1 = read_reg_or_limm(dc, a->c);
    sbc_op(cpu_regs[a->b], _rrl44_0, _rrl44_1, a->f);
    return true;
}

static bool trans_SBC_CC_U6(DisasContext *dc, arg_SBC_CC_U6 *a)
{
    sbc_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BIC(DisasContext *dc, arg_BIC *a)
{
    TCGv_i32 _rrl45_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl45_1 = read_reg_or_limm(dc, a->c);
    bic_op(cpu_regs[a->a], _rrl45_0, _rrl45_1, a->f);
    return true;
}

static bool trans_BIC_U6(DisasContext *dc, arg_BIC_U6 *a)
{
    bic_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BIC_S12(DisasContext *dc, arg_BIC_S12 *a)
{
    bic_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BIC_CC(DisasContext *dc, arg_BIC_CC *a)
{
    TCGv_i32 _rrl46_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl46_1 = read_reg_or_limm(dc, a->c);
    bic_op(cpu_regs[a->b], _rrl46_0, _rrl46_1, a->f);
    return true;
}

static bool trans_BIC_CC_U6(DisasContext *dc, arg_BIC_CC_U6 *a)
{
    bic_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BIC_S(DisasContext *dc, arg_BIC_S *a)
{
    tcg_gen_andc_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_TST(DisasContext *dc, arg_TST *a)
{
    tst_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_TST_U6(DisasContext *dc, arg_TST_U6 *a)
{
    tst_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_TST_S12(DisasContext *dc, arg_TST_S12 *a)
{
    tst_op(cpu_regs[a->b], tcg_constant_i32(a->s));
    return true;
}

static bool trans_TST_CC(DisasContext *dc, arg_TST_CC *a)
{
    tst_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_TST_CC_U6(DisasContext *dc, arg_TST_CC_U6 *a)
{
    tst_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_TST_S(DisasContext *dc, arg_TST_S *a)
{
    tst_op(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_BTST(DisasContext *dc, arg_BTST *a)
{
    btst_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_BTST_U6(DisasContext *dc, arg_BTST_U6 *a)
{
    btst_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_BTST_S12(DisasContext *dc, arg_BTST_S12 *a)
{
    btst_op(cpu_regs[a->b], tcg_constant_i32(a->s));
    return true;
}

static bool trans_BTST_CC(DisasContext *dc, arg_BTST_CC *a)
{
    btst_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_BTST_CC_U6(DisasContext *dc, arg_BTST_CC_U6 *a)
{
    btst_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_BTST_S(DisasContext *dc, arg_BTST_S *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_and_i32(tmp, read_reg_or_limm(dc, a->b), mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BSET(DisasContext *dc, arg_BSET *a)
{
    TCGv_i32 _rrl47_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl47_1 = read_reg_or_limm(dc, a->c);
    bset_op(cpu_regs[a->a], _rrl47_0, _rrl47_1, a->f);
    return true;
}

static bool trans_BSET_U6(DisasContext *dc, arg_BSET_U6 *a)
{
    bset_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BSET_S12(DisasContext *dc, arg_BSET_S12 *a)
{
    bset_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BSET_CC(DisasContext *dc, arg_BSET_CC *a)
{
    TCGv_i32 _rrl48_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl48_1 = read_reg_or_limm(dc, a->c);
    bset_op(cpu_regs[a->b], _rrl48_0, _rrl48_1, a->f);
    return true;
}

static bool trans_BSET_CC_U6(DisasContext *dc, arg_BSET_CC_U6 *a)
{
    bset_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BSET_S(DisasContext *dc, arg_BSET_S *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_or_i32(cpu_regs[a->b], read_reg_or_limm(dc, a->b), mask);
    return true;
}

static bool trans_BCLR(DisasContext *dc, arg_BCLR *a)
{
    TCGv_i32 _rrl49_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl49_1 = read_reg_or_limm(dc, a->c);
    bclr_op(cpu_regs[a->a], _rrl49_0, _rrl49_1, a->f);
    return true;
}

static bool trans_BCLR_U6(DisasContext *dc, arg_BCLR_U6 *a)
{
    bclr_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BCLR_S12(DisasContext *dc, arg_BCLR_S12 *a)
{
    bclr_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BCLR_CC(DisasContext *dc, arg_BCLR_CC *a)
{
    TCGv_i32 _rrl50_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl50_1 = read_reg_or_limm(dc, a->c);
    bclr_op(cpu_regs[a->b], _rrl50_0, _rrl50_1, a->f);
    return true;
}

static bool trans_BCLR_CC_U6(DisasContext *dc, arg_BCLR_CC_U6 *a)
{
    bclr_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BCLR_S(DisasContext *dc, arg_BCLR_S *a)
{
    TCGv_i32 bitpos = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_temp_new_i32();
    tcg_gen_andi_i32(bitpos, tcg_constant_i32(a->u), 31);
    tcg_gen_shl_i32(mask, tcg_constant_i32(1), bitpos);
    tcg_gen_andc_i32(cpu_regs[a->b], read_reg_or_limm(dc, a->b), mask);
    return true;
}

static bool trans_BXOR(DisasContext *dc, arg_BXOR *a)
{
    TCGv_i32 _rrl51_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl51_1 = read_reg_or_limm(dc, a->c);
    bxor_op(cpu_regs[a->a], _rrl51_0, _rrl51_1, a->f);
    return true;
}

static bool trans_BXOR_U6(DisasContext *dc, arg_BXOR_U6 *a)
{
    bxor_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BXOR_S12(DisasContext *dc, arg_BXOR_S12 *a)
{
    bxor_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BXOR_CC(DisasContext *dc, arg_BXOR_CC *a)
{
    TCGv_i32 _rrl52_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl52_1 = read_reg_or_limm(dc, a->c);
    bxor_op(cpu_regs[a->b], _rrl52_0, _rrl52_1, a->f);
    return true;
}

static bool trans_BXOR_CC_U6(DisasContext *dc, arg_BXOR_CC_U6 *a)
{
    bxor_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BMSK(DisasContext *dc, arg_BMSK *a)
{
    TCGv_i32 _rrl53_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl53_1 = read_reg_or_limm(dc, a->c);
    bmsk_op(cpu_regs[a->a], _rrl53_0, _rrl53_1, a->f);
    return true;
}

static bool trans_BMSK_U6(DisasContext *dc, arg_BMSK_U6 *a)
{
    bmsk_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BMSK_S12(DisasContext *dc, arg_BMSK_S12 *a)
{
    bmsk_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BMSK_CC(DisasContext *dc, arg_BMSK_CC *a)
{
    TCGv_i32 _rrl54_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl54_1 = read_reg_or_limm(dc, a->c);
    bmsk_op(cpu_regs[a->b], _rrl54_0, _rrl54_1, a->f);
    return true;
}

static bool trans_BMSK_CC_U6(DisasContext *dc, arg_BMSK_CC_U6 *a)
{
    bmsk_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BMSK_S(DisasContext *dc, arg_BMSK_S *a)
{
    uint32_t mask = (uint32_t)0xFFFFFFFF >> (31 - (a->u & 31));
    tcg_gen_andi_i32(cpu_regs[a->b], read_reg_or_limm(dc, a->b), mask);
    return true;

}

static bool trans_NEG_S(DisasContext *dc, arg_NEG_S *a)
{
    tcg_gen_neg_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_SWAP(DisasContext *dc, arg_SWAP *a)
{
    swap_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_SWAP_U6(DisasContext *dc, arg_SWAP_U6 *a)
{
    swap_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SWAPE(DisasContext *dc, arg_SWAPE *a)
{
    swape_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_SWAPE_U6(DisasContext *dc, arg_SWAPE_U6 *a)
{
    swape_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RLC(DisasContext *dc, arg_RLC *a)
{
    rlc_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_RLC_U6(DisasContext *dc, arg_RLC_U6 *a)
{
    rlc_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RRC(DisasContext *dc, arg_RRC *a)
{
    rrc_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_RRC_U6(DisasContext *dc, arg_RRC_U6 *a)
{
    rrc_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYM(DisasContext *dc, arg_MPYM *a)
{
    TCGv_i32 _rrl55_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl55_1 = read_reg_or_limm(dc, a->c);
    mpym_op(cpu_regs[a->a], _rrl55_0, _rrl55_1, a->f);
    return true;
}

static bool trans_MPYM_U6(DisasContext *dc, arg_MPYM_U6 *a)
{
    mpym_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYM_S12(DisasContext *dc, arg_MPYM_S12 *a)
{
    mpym_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYM_CC(DisasContext *dc, arg_MPYM_CC *a)
{
    TCGv_i32 _rrl56_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl56_1 = read_reg_or_limm(dc, a->c);
    mpym_op(cpu_regs[a->b], _rrl56_0, _rrl56_1, a->f);
    return true;
}

static bool trans_MPYM_CC_U6(DisasContext *dc, arg_MPYM_CC_U6 *a)
{
    mpym_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYMU(DisasContext *dc, arg_MPYMU *a)
{
    TCGv_i32 _rrl57_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl57_1 = read_reg_or_limm(dc, a->c);
    mpymu_op(cpu_regs[a->a], _rrl57_0, _rrl57_1, a->f);
    return true;
}

static bool trans_MPYMU_U6(DisasContext *dc, arg_MPYMU_U6 *a)
{
    mpymu_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYMU_S12(DisasContext *dc, arg_MPYMU_S12 *a)
{
    mpymu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYMU_CC(DisasContext *dc, arg_MPYMU_CC *a)
{
    TCGv_i32 _rrl58_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl58_1 = read_reg_or_limm(dc, a->c);
    mpymu_op(cpu_regs[a->b], _rrl58_0, _rrl58_1, a->f);
    return true;
}

static bool trans_MPYMU_CC_U6(DisasContext *dc, arg_MPYMU_CC_U6 *a)
{
    mpymu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYU(DisasContext *dc, arg_MPYU *a)
{
    TCGv_i32 _rrl59_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl59_1 = read_reg_or_limm(dc, a->c);
    mpyu_op(cpu_regs[a->a], _rrl59_0, _rrl59_1, a->f);
    return true;
}

static bool trans_MPYU_U6(DisasContext *dc, arg_MPYU_U6 *a)
{
    mpyu_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYU_S12(DisasContext *dc, arg_MPYU_S12 *a)
{
    mpyu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYU_CC(DisasContext *dc, arg_MPYU_CC *a)
{
    TCGv_i32 _rrl60_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl60_1 = read_reg_or_limm(dc, a->c);
    mpyu_op(cpu_regs[a->b], _rrl60_0, _rrl60_1, a->f);
    return true;
}

static bool trans_MPYU_CC_U6(DisasContext *dc, arg_MPYU_CC_U6 *a)
{
    mpyu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RSUB(DisasContext *dc, arg_RSUB *a)
{
    TCGv_i32 _rrl61_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl61_1 = read_reg_or_limm(dc, a->c);
    rsub_op(cpu_regs[a->a], _rrl61_0, _rrl61_1, a->f);
    return true;
}

static bool trans_RSUB_U6(DisasContext *dc, arg_RSUB_U6 *a)
{
    rsub_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RSUB_S12(DisasContext *dc, arg_RSUB_S12 *a)
{
    rsub_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_RSUB_CC(DisasContext *dc, arg_RSUB_CC *a)
{
    TCGv_i32 _rrl62_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl62_1 = read_reg_or_limm(dc, a->c);
    rsub_op(cpu_regs[a->b], _rrl62_0, _rrl62_1, a->f);
    return true;
}

static bool trans_RSUB_CC_U6(DisasContext *dc, arg_RSUB_CC_U6 *a)
{
    rsub_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SET(DisasContext *dc, arg_SET *a)
{
    TCGv_i32 _rrl63_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl63_1 = read_reg_or_limm(dc, a->c);
    set_op(cpu_regs[a->a], _rrl63_0, _rrl63_1, a->i, a->f);
    return true;
}

static bool trans_SET_U6(DisasContext *dc, arg_SET_U6 *a)
{
    set_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->i, a->f);
    return true;
}

static bool trans_SET_S12(DisasContext *dc, arg_SET_S12 *a)
{
    set_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->i, a->f);
    return true;
}

static bool trans_SET_CC(DisasContext *dc, arg_SET_CC *a)
{
    TCGv_i32 _rrl64_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl64_1 = read_reg_or_limm(dc, a->c);
    set_op(cpu_regs[a->b], _rrl64_0, _rrl64_1, a->i, a->f);
    return true;
}

static bool trans_SET_CC_U6(DisasContext *dc, arg_SET_CC_U6 *a)
{
    set_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->i, a->f);
    return true;
}

static bool trans_BMSKN(DisasContext *dc, arg_BMSKN *a)
{
    TCGv_i32 _rrl65_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl65_1 = read_reg_or_limm(dc, a->c);
    bmskn_op(cpu_regs[a->a], _rrl65_0, _rrl65_1, a->f);
    return true;
}

static bool trans_BMSKN_U6(DisasContext *dc, arg_BMSKN_U6 *a)
{
    bmskn_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BMSKN_S12(DisasContext *dc, arg_BMSKN_S12 *a)
{
    bmskn_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BMSKN_CC(DisasContext *dc, arg_BMSKN_CC *a)
{
    TCGv_i32 _rrl66_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl66_1 = read_reg_or_limm(dc, a->c);
    bmskn_op(cpu_regs[a->b], _rrl66_0, _rrl66_1, a->f);
    return true;
}

static bool trans_BMSKN_CC_U6(DisasContext *dc, arg_BMSKN_CC_U6 *a)
{
    bmskn_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYW(DisasContext *dc, arg_MPYW *a)
{
    TCGv_i32 _rrl67_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl67_1 = read_reg_or_limm(dc, a->c);
    mpyw_op(cpu_regs[a->a], _rrl67_0, _rrl67_1, a->f);
    return true;
}

static bool trans_MPYW_U6(DisasContext *dc, arg_MPYW_U6 *a)
{
    mpyw_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYW_S12(DisasContext *dc, arg_MPYW_S12 *a)
{
    mpyw_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYW_CC(DisasContext *dc, arg_MPYW_CC *a)
{
    TCGv_i32 _rrl68_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl68_1 = read_reg_or_limm(dc, a->c);
    mpyw_op(cpu_regs[a->b], _rrl68_0, _rrl68_1, a->f);
    return true;
}

static bool trans_MPYW_CC_U6(DisasContext *dc, arg_MPYW_CC_U6 *a)
{
    mpyw_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYW_S(DisasContext *dc, arg_MPYW_S *a)
{
    TCGv_i32 lo1 = tcg_temp_new_i32();
    TCGv_i32 lo2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(lo1, read_reg_or_limm(dc, a->b));
    tcg_gen_ext16s_i32(lo2, read_reg_or_limm(dc, a->c));
    tcg_gen_mul_i32(cpu_regs[a->b], lo1, lo2);
    return true;
}

static bool trans_MPYUW(DisasContext *dc, arg_MPYUW *a)
{
    TCGv_i32 _rrl69_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl69_1 = read_reg_or_limm(dc, a->c);
    mpywu_op(cpu_regs[a->a], _rrl69_0, _rrl69_1, a->f);
    return true;
}

static bool trans_MPYUW_U6(DisasContext *dc, arg_MPYUW_U6 *a)
{
    mpywu_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYUW_S12(DisasContext *dc, arg_MPYUW_S12 *a)
{
    mpywu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYUW_CC(DisasContext *dc, arg_MPYUW_CC *a)
{
    TCGv_i32 _rrl70_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl70_1 = read_reg_or_limm(dc, a->c);
    mpywu_op(cpu_regs[a->b], _rrl70_0, _rrl70_1, a->f);
    return true;
}

static bool trans_MPYUW_CC_U6(DisasContext *dc, arg_MPYUW_CC_U6 *a)
{
    mpywu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYUW_S(DisasContext *dc, arg_MPYUW_S *a)
{
    TCGv_i32 lo1 = tcg_temp_new_i32();
    TCGv_i32 lo2 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(lo1, read_reg_or_limm(dc, a->b));
    tcg_gen_ext16u_i32(lo2, read_reg_or_limm(dc, a->c));
    tcg_gen_mul_i32(cpu_regs[a->b], lo1, lo2);
    return true;
}

static bool trans_FFS(DisasContext *dc, arg_FFS *a)
{
    ffs_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_FFS_U6(DisasContext *dc, arg_FFS_U6 *a)
{
    ffs_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_FLS(DisasContext *dc, arg_FLS *a)
{
    fls_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_FLS_U6(DisasContext *dc, arg_FLS_U6 *a)
{
    fls_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_NORMH(DisasContext *dc, arg_NORMH *a)
{
    normh_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_NORMH_U6(DisasContext *dc, arg_NORMH_U6 *a)
{
    normh_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_NORM(DisasContext *dc, arg_NORM *a)
{
    norm_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_NORM_U6(DisasContext *dc, arg_NORM_U6 *a)
{
    norm_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ROL8(DisasContext *dc, arg_ROL8 *a)
{
    rol8_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c), a->f);
    return true;
}

static bool trans_ROL8_U6(DisasContext *dc, arg_ROL8_U6 *a)
{
    rol8_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_LR(DisasContext *dc, arg_LR *a)
{
    lr_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_LR_U6(DisasContext *dc, arg_LR_U6 *a)
{
    lr_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_LR_S12(DisasContext *dc, arg_LR_S12 *a)
{
    lr_op(cpu_regs[a->b], tcg_constant_i32(a->s));
    return true;
}

static bool trans_SR(DisasContext *dc, arg_SR *a)
{
    sr_op(cpu_regs[a->b], read_reg_or_limm(dc, a->c));
    return true;
}

static bool trans_SR_U6(DisasContext *dc, arg_SR_U6 *a)
{
    sr_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_SR_S12(DisasContext *dc, arg_SR_S12 *a)
{
    sr_op(cpu_regs[a->b], tcg_constant_i32(a->s));
    return true;
}

static bool trans_LD(DisasContext *dc, arg_LD *a)
{
    TCGv_i32 s1 = read_reg_or_limm(dc, a->b);
    TCGv_i32 s2 = read_reg_or_limm(dc, a->c);
    ld_op(cpu_regs[a->a], s1, s2, a->x, a->zz, a->aa);
    return true;
}

static bool trans_LD_S9(DisasContext *dc, arg_LD_S9 *a)
{
    if (a->zz == 3) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, read_reg_or_limm(dc, a->b));
        TCGv_i32 addr = tcg_temp_new_i32();
        if (a->aa == 2) {
            tcg_gen_mov_i32(addr, orig_b);
        } else if (a->aa == 3) {
            TCGv_i32 scaled_c = tcg_temp_new_i32();
            tcg_gen_shli_i32(scaled_c, tcg_constant_i32(a->s), 2);
            tcg_gen_add_i32(addr, orig_b, scaled_c);
        } else {
            tcg_gen_addi_i32(addr, orig_b, a->s);
        }
        tcg_gen_qemu_ld_i32(cpu_regs[a->a], addr, MMU_USER_IDX, MO_LEUL);
        TCGv_i32 addr2 = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr2, addr, 4);
        tcg_gen_qemu_ld_i32(cpu_regs[a->a + 1], addr2, MMU_USER_IDX, MO_LEUL);
        if (a->aa == 1) {
            tcg_gen_mov_i32(cpu_regs[a->b], addr);
        } else if (a->aa == 2) {
            tcg_gen_addi_i32(cpu_regs[a->b], orig_b, a->s);
        }
        return true;
    }
    ld_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->x, a->zz, a->aa);
    return true;
}

static bool trans_DIV(DisasContext *dc, arg_DIV *a)
{
    TCGv_i32 _rrl71_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl71_1 = read_reg_or_limm(dc, a->c);
    div_op(cpu_regs[a->a], _rrl71_0, _rrl71_1, a->f);
    return true;
}

static bool trans_DIV_U6(DisasContext *dc, arg_DIV_U6 *a)
{
    div_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u),a->f);
    return true;
}

static bool trans_DIV_S12(DisasContext *dc, arg_DIV_S12 *a)
{
    div_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s),a->f);
    return true;
}

static bool trans_DIV_CC(DisasContext *dc, arg_DIV_CC *a)
{
    TCGv_i32 _rrl72_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl72_1 = read_reg_or_limm(dc, a->c);
    div_op(cpu_regs[a->b], _rrl72_0, _rrl72_1, a->f);
    return true;
}

static bool trans_DIV_CC_U6(DisasContext *dc, arg_DIV_CC_U6 *a)
{
    div_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_DIVU(DisasContext *dc, arg_DIVU *a)
{
    TCGv_i32 _rrl73_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl73_1 = read_reg_or_limm(dc, a->c);
    divu_op(cpu_regs[a->a], _rrl73_0, _rrl73_1, a->f);
    return true;
}

static bool trans_DIVU_U6(DisasContext *dc, arg_DIVU_U6 *a)
{
    divu_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u),a->f);
    return true;
}

static bool trans_DIVU_S12(DisasContext *dc, arg_DIVU_S12 *a)
{
    divu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s),a->f);
    return true;
}

static bool trans_DIVU_CC(DisasContext *dc, arg_DIVU_CC *a)
{
    TCGv_i32 _rrl74_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl74_1 = read_reg_or_limm(dc, a->c);
    divu_op(cpu_regs[a->b], _rrl74_0, _rrl74_1, a->f);
    return true;
}

static bool trans_DIVU_CC_U6(DisasContext *dc, arg_DIVU_CC_U6 *a)
{
    divu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYD(DisasContext *dc, arg_MPYD *a)
{
    TCGv_i32 _rrl75_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl75_1 = read_reg_or_limm(dc, a->c);
    mpyd_op(cpu_regs[a->a], cpu_regs[a->a + 1], _rrl75_0, _rrl75_1, a->f);
    return true;
}

static bool trans_MPYD_U6(DisasContext *dc, arg_MPYD_U6 *a)
{
    mpyd_op(cpu_regs[a->a], cpu_regs[a->a + 1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u),a->f);
    return true;
}

static bool trans_MPYD_S12(DisasContext *dc, arg_MPYD_S12 *a)
{
    mpyd_op(cpu_regs[a->b], cpu_regs[a->b + 1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s),a->f);
    return true;
}

static bool trans_MPYD_CC(DisasContext *dc, arg_MPYD_CC *a)
{
    TCGv_i32 _rrl76_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl76_1 = read_reg_or_limm(dc, a->c);
    mpyd_op(cpu_regs[a->b], cpu_regs[a->b + 1], _rrl76_0, _rrl76_1, a->f);
    return true;
}

static bool trans_MPYD_CC_U6(DisasContext *dc, arg_MPYD_CC_U6 *a)
{
    mpyd_op(cpu_regs[a->b], cpu_regs[a->b + 1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYDU(DisasContext *dc, arg_MPYDU *a)
{
    TCGv_i32 _rrl77_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl77_1 = read_reg_or_limm(dc, a->c);
    mpydu_op(cpu_regs[a->a], cpu_regs[a->a + 1], _rrl77_0, _rrl77_1, a->f);
    return true;
}

static bool trans_MPYDU_U6(DisasContext *dc, arg_MPYDU_U6 *a)
{
    mpydu_op(cpu_regs[a->a], cpu_regs[a->a + 1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u),a->f);
    return true;
}

static bool trans_MPYDU_S12(DisasContext *dc, arg_MPYDU_S12 *a)
{
    mpydu_op(cpu_regs[a->b], cpu_regs[a->b + 1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s),a->f);
    return true;
}

static bool trans_MPYDU_CC(DisasContext *dc, arg_MPYDU_CC *a)
{
    TCGv_i32 _rrl78_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl78_1 = read_reg_or_limm(dc, a->c);
    mpydu_op(cpu_regs[a->b], cpu_regs[a->b + 1], _rrl78_0, _rrl78_1, a->f);
    return true;
}

static bool trans_MPYDU_CC_U6(DisasContext *dc, arg_MPYDU_CC_U6 *a)
{
    mpydu_op(cpu_regs[a->b], cpu_regs[a->b + 1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_DMACH(DisasContext *dc, arg_DMACH *a)
{
    TCGv_i32 _rrl79_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl79_1 = read_reg_or_limm(dc, a->c);
    dmach_op(cpu_regs[a->a], _rrl79_0, _rrl79_1);
    return true;
}

static bool trans_DMACH_U6(DisasContext *dc, arg_DMACH_U6 *a)
{
    dmach_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_DMACH_S12(DisasContext *dc, arg_DMACH_S12 *a)
{
    dmach_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_DMACH_CC(DisasContext *dc, arg_DMACH_CC *a)
{
    TCGv_i32 _rrl80_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl80_1 = read_reg_or_limm(dc, a->c);
    dmach_op(cpu_regs[a->b], _rrl80_0, _rrl80_1);
    return true;
}

static bool trans_DMACH_CC_U6(DisasContext *dc, arg_DMACH_CC_U6 *a)
{
    dmach_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_DMACHU(DisasContext *dc, arg_DMACHU *a)
{
    TCGv_i32 _rrl81_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl81_1 = read_reg_or_limm(dc, a->c);
    dmachu_op(cpu_regs[a->a], _rrl81_0, _rrl81_1);
    return true;
}

static bool trans_DMACHU_U6(DisasContext *dc, arg_DMACHU_U6 *a)
{
    dmachu_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_DMACHU_S12(DisasContext *dc, arg_DMACHU_S12 *a)
{
    dmachu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_DMACHU_CC(DisasContext *dc, arg_DMACHU_CC *a)
{
    TCGv_i32 _rrl82_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl82_1 = read_reg_or_limm(dc, a->c);
    dmachu_op(cpu_regs[a->b], _rrl82_0, _rrl82_1);
    return true;
}

static bool trans_DMACHU_CC_U6(DisasContext *dc, arg_DMACHU_CC_U6 *a)
{
    dmachu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMPY2H(DisasContext *dc, arg_VMPY2H *a)
{
    TCGv_i32 _rrl83_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl83_1 = read_reg_or_limm(dc, a->c);
    vmpy2h_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl83_0, _rrl83_1);
    return true;
}

static bool trans_VMPY2H_U6(DisasContext *dc, arg_VMPY2H_U6 *a)
{
    vmpy2h_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMPY2H_S12(DisasContext *dc, arg_VMPY2H_S12 *a)
{
    vmpy2h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VMPY2H_CC(DisasContext *dc, arg_VMPY2H_CC *a)
{
    TCGv_i32 _rrl84_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl84_1 = read_reg_or_limm(dc, a->c);
    vmpy2h_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl84_0, _rrl84_1);
    return true;
}

static bool trans_VMPY2H_CC_U6(DisasContext *dc, arg_VMPY2H_CC_U6 *a)
{
    vmpy2h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMPY2HU(DisasContext *dc, arg_VMPY2HU *a)
{
    TCGv_i32 _rrl85_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl85_1 = read_reg_or_limm(dc, a->c);
    vmpy2hu_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl85_0, _rrl85_1);
    return true;
}

static bool trans_VMPY2HU_U6(DisasContext *dc, arg_VMPY2HU_U6 *a)
{
    vmpy2hu_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMPY2HU_S12(DisasContext *dc, arg_VMPY2HU_S12 *a)
{
    vmpy2hu_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VMPY2HU_CC(DisasContext *dc, arg_VMPY2HU_CC *a)
{
    TCGv_i32 _rrl86_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl86_1 = read_reg_or_limm(dc, a->c);
    vmpy2hu_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl86_0, _rrl86_1);
    return true;
}

static bool trans_VMPY2HU_CC_U6(DisasContext *dc, arg_VMPY2HU_CC_U6 *a)
{
    vmpy2hu_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUB2(DisasContext *dc, arg_VSUB2 *a)
{
    TCGv_i32 _rrl87_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl87_1 = read_reg_or_limm(dc, a->c);
    vsub2_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl87_0, cpu_regs[a->b+1], _rrl87_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUB2_U6(DisasContext *dc, arg_VSUB2_U6 *a)
{
    vsub2_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUB2_S12(DisasContext *dc, arg_VSUB2_S12 *a)
{
    vsub2_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VSUB2_CC(DisasContext *dc, arg_VSUB2_CC *a)
{
    TCGv_i32 _rrl88_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl88_1 = read_reg_or_limm(dc, a->c);
    vsub2_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl88_0, cpu_regs[a->b+1], _rrl88_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUB2_CC_U6(DisasContext *dc, arg_VSUB2_CC_U6 *a)
{
    vsub2_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUB2H(DisasContext *dc, arg_VSUB2H *a)
{
    TCGv_i32 _rrl89_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl89_1 = read_reg_or_limm(dc, a->c);
    vsub2h_op(cpu_regs[a->a], _rrl89_0, _rrl89_1);
    return true;
}

static bool trans_VSUB2H_U6(DisasContext *dc, arg_VSUB2H_U6 *a)
{
    vsub2h_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUB2H_S12(DisasContext *dc, arg_VSUB2H_S12 *a)
{
    vsub2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VSUB2H_CC(DisasContext *dc, arg_VSUB2H_CC *a)
{
    TCGv_i32 _rrl90_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl90_1 = read_reg_or_limm(dc, a->c);
    vsub2h_op(cpu_regs[a->b], _rrl90_0, _rrl90_1);
    return true;
}

static bool trans_VSUB2H_CC_U6(DisasContext *dc, arg_VSUB2H_CC_U6 *a)
{
    vsub2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADD2(DisasContext *dc, arg_VADD2 *a)
{
    TCGv_i32 _rrl91_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl91_1 = read_reg_or_limm(dc, a->c);
    vadd2_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl91_0, cpu_regs[a->b+1], _rrl91_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADD2_U6(DisasContext *dc, arg_VADD2_U6 *a)
{
    vadd2_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADD2_S12(DisasContext *dc, arg_VADD2_S12 *a)
{
    vadd2_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VADD2_CC(DisasContext *dc, arg_VADD2_CC *a)
{
    TCGv_i32 _rrl92_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl92_1 = read_reg_or_limm(dc, a->c);
    vadd2_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl92_0, cpu_regs[a->b+1], _rrl92_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADD2_CC_U6(DisasContext *dc, arg_VADD2_CC_U6 *a)
{
    vadd2_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADD2H(DisasContext *dc, arg_VADD2H *a)
{
    TCGv_i32 _rrl93_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl93_1 = read_reg_or_limm(dc, a->c);
    vadd2h_op(cpu_regs[a->a], _rrl93_0, _rrl93_1);
    return true;
}

static bool trans_VADD2H_U6(DisasContext *dc, arg_VADD2H_U6 *a)
{
    vadd2h_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADD2H_S12(DisasContext *dc, arg_VADD2H_S12 *a)
{
    vadd2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VADD2H_CC(DisasContext *dc, arg_VADD2H_CC *a)
{
    TCGv_i32 _rrl94_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl94_1 = read_reg_or_limm(dc, a->c);
    vadd2h_op(cpu_regs[a->b], _rrl94_0, _rrl94_1);
    return true;
}

static bool trans_VADD2H_CC_U6(DisasContext *dc, arg_VADD2H_CC_U6 *a)
{
    vadd2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADD4H(DisasContext *dc, arg_VADD4H *a)
{
    TCGv_i32 _rrl95_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl95_1 = read_reg_or_limm(dc, a->c);
    vadd4h_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl95_0, cpu_regs[a->b+1], _rrl95_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADD4H_U6(DisasContext *dc, arg_VADD4H_U6 *a)
{
    vadd4h_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADD4H_S12(DisasContext *dc, arg_VADD4H_S12 *a)
{
    vadd4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VADD4H_CC(DisasContext *dc, arg_VADD4H_CC *a)
{
    TCGv_i32 _rrl96_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl96_1 = read_reg_or_limm(dc, a->c);
    vadd4h_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl96_0, cpu_regs[a->b+1], _rrl96_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADD4H_CC_U6(DisasContext *dc, arg_VADD4H_CC_U6 *a)
{
    vadd4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADDSUB(DisasContext *dc, arg_VADDSUB *a)
{
    TCGv_i32 _rrl97_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl97_1 = read_reg_or_limm(dc, a->c);
    vaddsub_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl97_0, cpu_regs[a->b+1], _rrl97_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADDSUB_U6(DisasContext *dc, arg_VADDSUB_U6 *a)
{
    vaddsub_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADDSUB_S12(DisasContext *dc, arg_VADDSUB_S12 *a)
{
    vaddsub_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VADDSUB_CC(DisasContext *dc, arg_VADDSUB_CC *a)
{
    TCGv_i32 _rrl98_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl98_1 = read_reg_or_limm(dc, a->c);
    vaddsub_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl98_0, cpu_regs[a->b+1], _rrl98_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADDSUB_CC_U6(DisasContext *dc, arg_VADDSUB_CC_U6 *a)
{
    vaddsub_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADDSUB2H(DisasContext *dc, arg_VADDSUB2H *a)
{
    TCGv_i32 _rrl99_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl99_1 = read_reg_or_limm(dc, a->c);
    vaddsub2h_op(cpu_regs[a->a], _rrl99_0, _rrl99_1);
    return true;
}

static bool trans_VADDSUB2H_U6(DisasContext *dc, arg_VADDSUB2H_U6 *a)
{
    vaddsub2h_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADDSUB2H_S12(DisasContext *dc, arg_VADDSUB2H_S12 *a)
{
    vaddsub2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VADDSUB2H_CC(DisasContext *dc, arg_VADDSUB2H_CC *a)
{
    TCGv_i32 _rrl100_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl100_1 = read_reg_or_limm(dc, a->c);
    vaddsub2h_op(cpu_regs[a->b], _rrl100_0, _rrl100_1);
    return true;
}

static bool trans_VADDSUB2H_CC_U6(DisasContext *dc, arg_VADDSUB2H_CC_U6 *a)
{
    vaddsub2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADDSUB4H(DisasContext *dc, arg_VADDSUB4H *a)
{
    TCGv_i32 _rrl101_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl101_1 = read_reg_or_limm(dc, a->c);
    vaddsub4h_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl101_0, cpu_regs[a->b+1], _rrl101_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADDSUB4H_U6(DisasContext *dc, arg_VADDSUB4H_U6 *a)
{
    vaddsub4h_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VADDSUB4H_S12(DisasContext *dc, arg_VADDSUB4H_S12 *a)
{
    vaddsub4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VADDSUB4H_CC(DisasContext *dc, arg_VADDSUB4H_CC *a)
{
    TCGv_i32 _rrl102_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl102_1 = read_reg_or_limm(dc, a->c);
    vaddsub4h_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl102_0, cpu_regs[a->b+1], _rrl102_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VADDSUB4H_CC_U6(DisasContext *dc, arg_VADDSUB4H_CC_U6 *a)
{
    vaddsub4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUBADD(DisasContext *dc, arg_VSUBADD *a)
{
    TCGv_i32 _rrl103_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl103_1 = read_reg_or_limm(dc, a->c);
    vsubadd_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl103_0, cpu_regs[a->b+1], _rrl103_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUBADD_U6(DisasContext *dc, arg_VSUBADD_U6 *a)
{
    vsubadd_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUBADD_S12(DisasContext *dc, arg_VSUBADD_S12 *a)
{
    vsubadd_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VSUBADD_CC(DisasContext *dc, arg_VSUBADD_CC *a)
{
    TCGv_i32 _rrl104_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl104_1 = read_reg_or_limm(dc, a->c);
    vsubadd_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl104_0, cpu_regs[a->b+1], _rrl104_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUBADD_CC_U6(DisasContext *dc, arg_VSUBADD_CC_U6 *a)
{
    vsubadd_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUBADD2H(DisasContext *dc, arg_VSUBADD2H *a)
{
    TCGv_i32 _rrl105_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl105_1 = read_reg_or_limm(dc, a->c);
    vsubadd2h_op(cpu_regs[a->a], _rrl105_0, _rrl105_1);
    return true;
}

static bool trans_VSUBADD2H_U6(DisasContext *dc, arg_VSUBADD2H_U6 *a)
{
    vsubadd2h_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUBADD2H_S12(DisasContext *dc, arg_VSUBADD2H_S12 *a)
{
    vsubadd2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VSUBADD2H_CC(DisasContext *dc, arg_VSUBADD2H_CC *a)
{
    TCGv_i32 _rrl106_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl106_1 = read_reg_or_limm(dc, a->c);
    vsubadd2h_op(cpu_regs[a->b], _rrl106_0, _rrl106_1);
    return true;
}

static bool trans_VSUBADD2H_CC_U6(DisasContext *dc, arg_VSUBADD2H_CC_U6 *a)
{
    vsubadd2h_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUBADD4H(DisasContext *dc, arg_VSUBADD4H *a)
{
    TCGv_i32 _rrl107_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl107_1 = read_reg_or_limm(dc, a->c);
    vsubadd4h_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl107_0, cpu_regs[a->b+1], _rrl107_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUBADD4H_U6(DisasContext *dc, arg_VSUBADD4H_U6 *a)
{
    vsubadd4h_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUBADD4H_S12(DisasContext *dc, arg_VSUBADD4H_S12 *a)
{
    vsubadd4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VSUBADD4H_CC(DisasContext *dc, arg_VSUBADD4H_CC *a)
{
    TCGv_i32 _rrl108_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl108_1 = read_reg_or_limm(dc, a->c);
    vsubadd4h_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl108_0, cpu_regs[a->b+1], _rrl108_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUBADD4H_CC_U6(DisasContext *dc, arg_VSUBADD4H_CC_U6 *a)
{
    vsubadd4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMAC2H(DisasContext *dc, arg_VMAC2H *a)
{
    TCGv_i32 _rrl109_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl109_1 = read_reg_or_limm(dc, a->c);
    vmac2h_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl109_0, _rrl109_1);
    return true;
}

static bool trans_VMAC2H_U6(DisasContext *dc, arg_VMAC2H_U6 *a)
{
    vmac2h_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMAC2H_S12(DisasContext *dc, arg_VMAC2H_S12 *a)
{
    vmac2h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VMAC2H_CC(DisasContext *dc, arg_VMAC2H_CC *a)
{
    TCGv_i32 _rrl110_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl110_1 = read_reg_or_limm(dc, a->c);
    vmac2h_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl110_0, _rrl110_1);
    return true;
}

static bool trans_VMAC2H_CC_U6(DisasContext *dc, arg_VMAC2H_CC_U6 *a)
{
    vmac2h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMAC2HU(DisasContext *dc, arg_VMAC2HU *a)
{
    TCGv_i32 _rrl111_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl111_1 = read_reg_or_limm(dc, a->c);
    vmac2hu_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl111_0, _rrl111_1);
    return true;
}

static bool trans_VMAC2HU_U6(DisasContext *dc, arg_VMAC2HU_U6 *a)
{
    vmac2hu_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VMAC2HU_S12(DisasContext *dc, arg_VMAC2HU_S12 *a)
{
    vmac2hu_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VMAC2HU_CC(DisasContext *dc, arg_VMAC2HU_CC *a)
{
    TCGv_i32 _rrl112_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl112_1 = read_reg_or_limm(dc, a->c);
    vmac2hu_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl112_0, _rrl112_1);
    return true;
}

static bool trans_VMAC2HU_CC_U6(DisasContext *dc, arg_VMAC2HU_CC_U6 *a)
{
    vmac2hu_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUB4H(DisasContext *dc, arg_VSUB4H *a)
{
    TCGv_i32 _rrl113_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl113_1 = read_reg_or_limm(dc, a->c);
    vsub4h_op(cpu_regs[a->a], cpu_regs[a->a+1], _rrl113_0, cpu_regs[a->b+1], _rrl113_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUB4H_U6(DisasContext *dc, arg_VSUB4H_U6 *a)
{
    vsub4h_op(cpu_regs[a->a], cpu_regs[a->a+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_VSUB4H_S12(DisasContext *dc, arg_VSUB4H_S12 *a)
{
    vsub4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->s), tcg_constant_i32(a->s));
    return true;
}

static bool trans_VSUB4H_CC(DisasContext *dc, arg_VSUB4H_CC *a)
{
    TCGv_i32 _rrl114_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl114_1 = read_reg_or_limm(dc, a->c);
    vsub4h_op(cpu_regs[a->b], cpu_regs[a->b+1], _rrl114_0, cpu_regs[a->b+1], _rrl114_1, cpu_regs[a->c+1]);
    return true;
}

static bool trans_VSUB4H_CC_U6(DisasContext *dc, arg_VSUB4H_CC_U6 *a)
{
    vsub4h_op(cpu_regs[a->b], cpu_regs[a->b+1], read_reg_or_limm(dc, a->b), cpu_regs[a->b+1], tcg_constant_i32(a->u), tcg_constant_i32(a->u));
    return true;
}

static bool trans_TRAP_S(DisasContext *dc, arg_TRAP_S *a)
{
    tcg_gen_movi_i32(cpu_pc, dc->base.pc_next + 2);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(EXCP_SYSCALL));
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_LD_S_H_U5(DisasContext *dc, arg_LD_S_H_U5 *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[a->h], a->u * 4);
    tcg_gen_qemu_ld_i32(cpu_regs[a->a], addr, MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_PUSH_S(DisasContext *dc, arg_PUSH_S *a)
{
    tcg_gen_subi_i32(cpu_regs[28], cpu_regs[28], 4);
    tcg_gen_qemu_st_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[28], MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_PUSH_S_BLINK(DisasContext *dc, arg_PUSH_S_BLINK *a)
{
    tcg_gen_subi_i32(cpu_regs[28], cpu_regs[28], 4);
    tcg_gen_qemu_st_i32(cpu_regs[31], cpu_regs[28], MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_MOV_S_GH(DisasContext *dc, arg_MOV_S_GH *a)
{
    if (a->h == 30) {
        uint16_t limm_hi = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 2, MO_LE);
        uint16_t limm_lo = translator_lduw_end(dc->env, &dc->base, dc->base.pc_next + 4, MO_LE);
        uint32_t limm = (limm_hi << 16) | limm_lo;
        tcg_gen_movi_i32(cpu_regs[a->g], limm);
        dc->base.pc_next += 4;
    } else {
        tcg_gen_mov_i32(cpu_regs[a->g], cpu_regs[a->h]);
    }
    return true;
}

static bool trans_BL(DisasContext *dc, arg_BL *a)
{
    uint32_t target = (dc->base.pc_next & ~3) + (a->s * 4);
    tcg_gen_movi_i32(cpu_regs[31], dc->base.pc_next + 4);
    tcg_gen_movi_i32(cpu_pc, target);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_BL_D(DisasContext *dc, arg_BL_D *a)
{
    uint32_t target = (dc->base.pc_next & ~3) + (a->s * 4);
    dc->has_delay_slot = true;
    dc->has_delay_link = true;
    dc->delay_target = tcg_constant_i32(target);
    return true;
}

static bool trans_ST_S9(DisasContext *dc, arg_ST_S9 *a)
{
    if (a->zz == 3) {
        TCGv_i32 orig_b = tcg_temp_new_i32();
        tcg_gen_mov_i32(orig_b, read_reg_or_limm(dc, a->b));
        TCGv_i32 addr = tcg_temp_new_i32();
        if (a->aa == 2) {
            tcg_gen_mov_i32(addr, orig_b);
        } else if (a->aa == 3) {
            TCGv_i32 scaled_c = tcg_temp_new_i32();
            tcg_gen_shli_i32(scaled_c, tcg_constant_i32(a->s), 2);
            tcg_gen_add_i32(addr, orig_b, scaled_c);
        } else {
            tcg_gen_addi_i32(addr, orig_b, a->s);
        }
        tcg_gen_qemu_st_i32(cpu_regs[a->c], addr, MMU_USER_IDX, MO_LEUL);
        TCGv_i32 addr2 = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr2, addr, 4);
        tcg_gen_qemu_st_i32(cpu_regs[a->c + 1], addr2, MMU_USER_IDX, MO_LEUL);
        if (a->aa == 1) {
            tcg_gen_mov_i32(cpu_regs[a->b], addr);
        } else if (a->aa == 2) {
            tcg_gen_addi_i32(cpu_regs[a->b], orig_b, a->s);
        }
        return true;
    }
    TCGv_i32 _rrl115_0 = read_reg_or_limm(dc, a->c);
    TCGv_i32 _rrl115_1 = read_reg_or_limm(dc, a->b);
    st_op(_rrl115_0, _rrl115_1, tcg_constant_i32(a->s), a->zz, a->aa);
    return true;
}

static bool trans_ST_S9_IMM(DisasContext *dc, arg_ST_S9_IMM *a)
{
    st_op(tcg_constant_i32(a->c), read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->zz, a->aa);
    return true;
}

static bool trans_ST_S(DisasContext *dc, arg_ST_S *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[arc_reduced_regs[a->b]], a->u * 4);
    tcg_gen_qemu_st_i32(cpu_regs[arc_reduced_regs[a->c]], addr, MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_LD_S_GEN(DisasContext *dc, arg_LD_S_GEN *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[arc_reduced_regs[a->b]], a->u * 4);
    tcg_gen_qemu_ld_i32(cpu_regs[arc_reduced_regs[a->c]], addr, MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_BRCC_S(DisasContext *dc, arg_BRCC_S *a)
{
    uint32_t target = (dc->base.pc_next & ~3) + (a->s * 2);
    uint32_t fallthrough = dc->base.pc_next + 2;
    TCGCond cond = a->cond ? TCG_COND_NE : TCG_COND_EQ;
    tcg_gen_movcond_i32(cond, cpu_pc, cpu_regs[arc_reduced_regs[a->b]], tcg_constant_i32(0), tcg_constant_i32(target), tcg_constant_i32(fallthrough));
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_LP(DisasContext *dc, arg_LP *a)
{
    uint32_t lp_end_val = (dc->base.pc_next & ~3) + (a->s * 2);
    uint32_t next_pc = dc->base.pc_next + 4;
    tcg_gen_movi_i32(cpu_lp_end, lp_end_val);
    tcg_gen_movi_i32(cpu_lp_start, next_pc);
    tcg_gen_movi_i32(cpu_pc, next_pc);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_LPCC(DisasContext *dc, arg_LPCC *a)
{
    uint32_t lp_end_val = (dc->base.pc_next & ~3) + (a->s * 2);
    uint32_t next_pc = dc->base.pc_next + 4;
    TCGv_i32 cond = gen_cc_test(a->q);
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_pc, cond, tcg_constant_i32(1),tcg_constant_i32(next_pc), tcg_constant_i32(lp_end_val));
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_lp_end, cond, tcg_constant_i32(1),tcg_constant_i32(lp_end_val), cpu_lp_end);
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_lp_start, cond, tcg_constant_i32(1),tcg_constant_i32(next_pc), cpu_lp_start);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_XBFU(DisasContext *dc, arg_XBFU *a)
{
    TCGv_i32 _rrl116_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl116_1 = read_reg_or_limm(dc, a->c);
    xbfu_op(cpu_regs[a->a], _rrl116_0, _rrl116_1, a->f);
    return true;
}

static bool trans_XBFU_U6(DisasContext *dc, arg_XBFU_U6 *a)
{
    xbfu_op(cpu_regs[a->a], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_POP_S(DisasContext *dc, arg_POP_S *a)
{
    tcg_gen_qemu_ld_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[28], MMU_USER_IDX, MO_LEUL);
    tcg_gen_addi_i32(cpu_regs[28], cpu_regs[28], 4);
    return true;
}

static bool trans_POP_S_BLINK(DisasContext *dc, arg_POP_S_BLINK *a)
{
    tcg_gen_qemu_ld_i32(cpu_regs[31], cpu_regs[28], MMU_USER_IDX, MO_LEUL);
    tcg_gen_addi_i32(cpu_regs[28], cpu_regs[28], 4);
    return true;
}

static bool trans_J_S(DisasContext *dc, arg_J_S *a)
{
    tcg_gen_mov_i32(cpu_pc, cpu_regs[arc_reduced_regs[a->b]]);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_J_S_D(DisasContext *dc, arg_J_S_D *a)
{
    dc->has_delay_slot = true;
    dc->delay_target = tcg_temp_new_i32();
    tcg_gen_mov_i32(dc->delay_target, cpu_regs[arc_reduced_regs[a->b]]);
    return true;
}

static bool trans_JL_S(DisasContext *dc, arg_JL_S *a)
{
    tcg_gen_movi_i32(cpu_regs[31], dc->base.pc_next + 2);
    tcg_gen_mov_i32(cpu_pc, cpu_regs[arc_reduced_regs[a->b]]);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_JL_S_D(DisasContext *dc, arg_JL_S_D *a)
{
    dc->has_delay_slot = true;
    dc->has_delay_link = true;
    dc->delay_target = tcg_temp_new_i32();
    tcg_gen_mov_i32(dc->delay_target, cpu_regs[arc_reduced_regs[a->b]]);
    return true;
}

static bool trans_J_S_BLINK(DisasContext *dc, arg_J_S_BLINK *a)
{
    tcg_gen_mov_i32(cpu_pc, cpu_regs[31]);
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_JEQ_S_BLINK(DisasContext *dc, arg_JEQ_S_BLINK *a)
{
    uint32_t fallthrough = dc->base.pc_next + 2;
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_pc, cpu_zf, tcg_constant_i32(1), cpu_regs[31], tcg_constant_i32(fallthrough));
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_JNE_S_BLINK(DisasContext *dc, arg_JNE_S_BLINK *a)
{
    uint32_t fallthrough = dc->base.pc_next + 2;
    tcg_gen_movcond_i32(TCG_COND_NE, cpu_pc, cpu_zf, tcg_constant_i32(1), cpu_regs[31], tcg_constant_i32(fallthrough));
    dc->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_J_S_D_BLINK(DisasContext *dc, arg_J_S_D_BLINK *a)
{
    dc->has_delay_slot = true;
    dc->delay_target = tcg_temp_new_i32();
    tcg_gen_mov_i32(dc->delay_target, cpu_regs[31]);
    return true;
}

static bool trans_ST_S_SP(DisasContext *dc, arg_ST_S_SP *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[28], a->u * 4);
    tcg_gen_qemu_st_i32(cpu_regs[arc_reduced_regs[a->b]], addr, MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_LD_S_SP(DisasContext *dc, arg_LD_S_SP *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[28], a->u * 4);
    tcg_gen_qemu_ld_i32(cpu_regs[arc_reduced_regs[a->b]], addr, MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_LDB_S_SP(DisasContext *dc, arg_LDB_S_SP *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[28], a->u * 4);
    tcg_gen_qemu_ld_i32(cpu_regs[arc_reduced_regs[a->b]], addr, MMU_USER_IDX, MO_UB);
    return true;
}

static bool trans_STB_S_SP(DisasContext *dc, arg_STB_S_SP *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[28], a->u * 4);
    tcg_gen_qemu_st_i32(cpu_regs[arc_reduced_regs[a->b]], addr, MMU_USER_IDX, MO_UB);
    return true;
}

static bool trans_LDB_S_RR(DisasContext *dc, arg_LDB_S_RR *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    tcg_gen_qemu_ld_i32(cpu_regs[arc_reduced_regs[a->a]], addr, MMU_USER_IDX, MO_UB);
    return true;
}

static bool trans_LLOCK(DisasContext *dc, arg_LLOCK *a)
{
    TCGv_i32 base = read_reg_or_limm(dc, a->c);
    tcg_gen_qemu_ld_i32(cpu_regs[a->b], base, MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_SCOND(DisasContext *dc, arg_SCOND *a)
{
    TCGv_i32 base = read_reg_or_limm(dc, a->c);
    TCGv_i32 val = read_reg_or_limm(dc, a->b);
    tcg_gen_qemu_st_i32(val, base, MMU_USER_IDX, MO_LEUL);
    tcg_gen_movi_i32(cpu_zf, 1);
    return true;
}

static bool trans_EX(DisasContext *dc, arg_EX *a)
{
    TCGv_i32 addr = read_reg_or_limm(dc, a->c);
    tcg_gen_atomic_xchg_i32(cpu_regs[a->b], addr, cpu_regs[a->b], MMU_USER_IDX, MO_LEUL);
    return true;
}

static bool trans_LDB_S_GEN(DisasContext *dc, arg_LDB_S_GEN *a)
{   
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[arc_reduced_regs[a->b]], a->u);
    tcg_gen_qemu_ld_i32(cpu_regs[arc_reduced_regs[a->c]], addr, MMU_USER_IDX, MO_UB);
    return true;
}

static bool trans_SUB_S(DisasContext *dc, arg_SUB_S *a)
{   
    tcg_gen_sub_i32(cpu_regs[arc_reduced_regs[a->a]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_DMB(DisasContext *dc, arg_DMB *a)
{   
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    return true;
}

static bool trans_STB_S(DisasContext *dc, arg_STB_S *a)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_addi_i32(addr, cpu_regs[arc_reduced_regs[a->b]], a->u);
    tcg_gen_qemu_st_i32(cpu_regs[arc_reduced_regs[a->c]], addr, MMU_USER_IDX, MO_UB);
    return true;
}

static bool trans_REMU(DisasContext *dc, arg_REMU *a)
{
    TCGv_i32 s1 = read_reg_or_limm(dc, a->b);
    TCGv_i32 s2 = read_reg_or_limm(dc, a->c);
    remu_op(cpu_regs[a->a], s1, s2, a->f);
    return true;
}

static bool trans_REMU_U6(DisasContext *dc, arg_REMU_U6 *a)
{
    remu_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_REMU_S12(DisasContext *dc, arg_REMU_S12 *a)
{
    remu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_REMU_CC(DisasContext *dc, arg_REMU_CC *a)
{
    TCGv_i32 _rrl_remu_cc_0 = read_reg_or_limm(dc, a->b);
    TCGv_i32 _rrl_remu_cc_1 = read_reg_or_limm(dc, a->c);
    remu_op(cpu_regs[a->b], _rrl_remu_cc_0, _rrl_remu_cc_1, a->f);
    return true;
}

static bool trans_REMU_CC_U6(DisasContext *dc, arg_REMU_CC_U6 *a)
{
    remu_op(cpu_regs[a->b], read_reg_or_limm(dc, a->b), tcg_constant_i32(a->u), a->f);
    return true;
}

static void gen_check_loop_end(DisasContext *dc)
{
    if (dc->base.pc_next != dc->lp_end) {
        return;
    }
    TCGLabel *label_exit = gen_new_label();
    TCGLabel *label_done = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[60], 1, label_exit);
    tcg_gen_subi_i32(cpu_regs[60], cpu_regs[60], 1);
    if (dc->lp_start_off) {
        tcg_gen_movi_i32(cpu_pc, dc->base.pc_next - dc->lp_start_off);
    } else {
        tcg_gen_mov_i32(cpu_pc, cpu_lp_start);
    }
    tcg_gen_br(label_done);
    gen_set_label(label_exit);
    tcg_gen_movi_i32(cpu_pc, dc->base.pc_next);
    gen_set_label(label_done);
    dc->base.is_jmp = DISAS_NORETURN;
}

static void arc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    bool had_pending_delay_slot = dc->has_delay_slot;
    uint16_t insn_hi = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next, MO_LE);
    uint16_t op5 = (insn_hi >> 11) & 0x1F;
    if (op5 == 0x00 || op5 == 0x01 || op5 == 0x02 ||  op5 ==0x03 || op5 == 0x04 || op5 == 0x05) {
        uint16_t insn_lo = translator_lduw_end(cpu_env(cs), &dc->base, dc->base.pc_next + 2, MO_LE);
        uint32_t insn = (insn_hi << 16) | insn_lo;
        TCGLabel *label_skip = gen_new_label();
        if ((extract32(insn, 27, 5) == 0x04 || extract32(insn, 27, 5) == 0x05) && extract32(insn, 22, 2) == 3 && extract32(insn, 19, 3) != 0x6) {
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
        if (dc->has_delay_link) {
            tcg_gen_movi_i32(cpu_regs[31], dc->base.pc_next);
            dc->has_delay_link = false;
        }
        if (dc->delay_is_cond) {
            tcg_gen_movcond_i32(TCG_COND_NE, cpu_pc, dc->delay_cond_taken, tcg_constant_i32(0),
                                 dc->delay_target, tcg_constant_i32(dc->base.pc_next));
            dc->delay_is_cond = false;
        } else {
            tcg_gen_mov_i32(cpu_pc, dc->delay_target);
        }
        dc->base.is_jmp = DISAS_NORETURN;
        dc->has_delay_slot = false;
    }
    if (dc->base.is_jmp != DISAS_NORETURN) {
        gen_check_loop_end(dc);
    }

}

static void arc_tr_init_disas_context(DisasContextBase *db, CPUState *cs)
{
    DisasContext *dc = container_of(db, DisasContext, base);
    dc->env = cpu_env(cs);
    dc->lp_start_off = (db->tb->cs_base & ARC_CSBASE_LBEG_OFF_MASK) >> ARC_CSBASE_LBEG_OFF_SHIFT;
    dc->lp_end = (db->tb->cs_base & ARC_CSBASE_LEND_MASK) + (db->pc_first & TARGET_PAGE_MASK);
}

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

void arc_translate_code(CPUState *cs, TranslationBlock *tb, int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext dc = { };
    translator_loop(cs, tb, max_insns, pc, host_pc, &arc_tr_ops, &dc.base);
}