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

static bool trans_MOV(DisasContext *dc, arg_MOV *a)
{
    mov(cpu_regs[a->b], cpu_regs[a->c], a->f);
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
    mov(cpu_regs[a->b], cpu_regs[a->c], a->f);
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
    add(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD_U6(DisasContext *dc, arg_ADD_U6 *a)
{
    add(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD_S12(DisasContext *dc, arg_ADD_S12 *a)
{
    add(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD_CC_F(DisasContext *dc, arg_ADD_CC_F *a)
{
    add(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD_CC_F_U6(DisasContext *dc, arg_ADD_CC_F_U6 *a)
{
    add(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD1(DisasContext *dc, arg_ADD1 *a)
{   
    add1(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD1_U6(DisasContext *dc, arg_ADD1_U6 *a)
{
    add1(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD1_S12(DisasContext *dc, arg_ADD1_S12 *a)
{
    add1(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD1_CC_F(DisasContext *dc, arg_ADD_CC_F *a)
{
    add1(cpu_regs[a->b], cpu_regs[a->b],  cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD1_CC_F_U6(DisasContext *dc, arg_ADD1_CC_F_U6 *a)
{
    add1(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD2(DisasContext *dc, arg_ADD2 *a)
{   
    add2(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD2_U6(DisasContext *dc, arg_ADD2_U6 *a)
{
    add2(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD2_S12(DisasContext *dc, arg_ADD2_S12 *a)
{
    add2(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD2_CC_F(DisasContext *dc, arg_ADD2_CC_F *a)
{
    add2(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD2_CC_F_U6(DisasContext *dc, arg_ADD2_CC_F_U6 *a)
{
    add2(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD3(DisasContext *dc, arg_ADD3 *a)
{   
    add3(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD3_U6(DisasContext *dc, arg_ADD3_U6 *a)
{
    add3(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADD3_S12(DisasContext *dc, arg_ADD3_S12 *a)
{
    add3(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADD3_CC_F(DisasContext *dc, arg_ADD3_CC_F *a)
{
    add3(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADD3_CC_F_U6(DisasContext *dc, arg_ADD3_CC_F_U6 *a)
{
    add3(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    mpy(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPY_u6(DisasContext *dc, arg_MPY_u6 *a)
{
    mpy(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPY_s12(DisasContext *dc, arg_MPY_s12 *a)
{
    mpy(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPY_CC_F(DisasContext *dc, arg_MPY_CC_F *a)
{
    mpy(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPY_CC_F_U6(DisasContext *dc, arg_MPY_CC_F_U6 *a)
{
    mpy(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPY_S(DisasContext *dc, arg_MPY_S *a)
{
    tcg_gen_mul_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_SUB(DisasContext *dc, arg_SUB *a)
{
    sub(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB_u6(DisasContext *dc, arg_SUB_u6 *a)
{
    sub(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB_s12(DisasContext *dc, arg_SUB_s12 *a)
{
    sub(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB_CC(DisasContext *dc, arg_SUB_CC *a)
{
    sub(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB_CC_U6(DisasContext *dc, arg_SUB_CC_U6 *a)
{
    sub(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB1(DisasContext *dc, arg_SUB1 *a)
{
    sub1(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB1_U6(DisasContext *dc, arg_SUB1_U6 *a)
{
    sub1(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB1_S12(DisasContext *dc, arg_SUB1_S12 *a)
{
    sub1(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB1_CC(DisasContext *dc, arg_SUB1_CC *a)
{
    sub1(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB1_CC_U6(DisasContext *dc, arg_SUB1_CC_U6 *a)
{
    sub1(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB2(DisasContext *dc, arg_SUB2 *a)
{
    sub2(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB2_U6(DisasContext *dc, arg_SUB2_U6 *a)
{
    sub2(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB2_S12(DisasContext *dc, arg_SUB2_S12 *a)
{
    sub2(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB2_CC(DisasContext *dc, arg_SUB2_CC *a)
{
    sub2(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB2_CC_U6(DisasContext *dc, arg_SUB2_CC_U6 *a)
{
    sub2(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB3(DisasContext *dc, arg_SUB3 *a)
{
    sub3(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB3_U6(DisasContext *dc, arg_SUB3_U6 *a)
{
    sub3(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SUB3_S12(DisasContext *dc, arg_SUB3_S12 *a)
{
    sub3(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SUB3_CC(DisasContext *dc, arg_SUB3_CC *a)
{
    sub3(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SUB3_CC_U6(DisasContext *dc, arg_SUB3_CC_U6 *a)
{
    sub3(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    and_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_AND_U6(DisasContext *dc, arg_AND_U6 *a)
{
    and_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_AND_S12(DisasContext *dc, arg_AND_S12 *a)
{
    and_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_AND_CC(DisasContext *dc, arg_AND_CC *a)
{
    and_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_AND_CC_U6(DisasContext *dc, arg_AND_CC_U6 *a)
{
    and_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_AND_S(DisasContext *dc, arg_AND_S *a)
{
    tcg_gen_and_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_OR(DisasContext *dc, arg_OR *a)
{
    or_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_OR_U6(DisasContext *dc, arg_OR_U6 *a)
{
    or_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_OR_S12(DisasContext *dc, arg_OR_S12 *a)
{
    or_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_OR_CC(DisasContext *dc, arg_OR_CC *a)
{
    or_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_OR_CC_U6(DisasContext *dc, arg_OR_CC_U6 *a)
{
    or_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_OR_S(DisasContext *dc, arg_OR_S *a)
{
    tcg_gen_or_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_XOR(DisasContext *dc, arg_XOR *a)
{
    xor_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_XOR_U6(DisasContext *dc, arg_XOR_U6 *a)
{
    xor_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_XOR_S12(DisasContext *dc, arg_XOR_S12 *a)
{
    xor_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_XOR_CC(DisasContext *dc, arg_XOR_CC *a)
{
    xor_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_XOR_CC_U6(DisasContext *dc, arg_XOR_CC_U6 *a)
{
    xor_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_XOR_S(DisasContext *dc, arg_XOR_S *a)
{
    tcg_gen_xor_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_ASL(DisasContext *dc, arg_ASL *a)
{
    asl_simple(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ASL_U6(DisasContext *dc, arg_ASL_U6 *a)
{
    asl_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASL_F(DisasContext *dc, arg_ASL_F *a)
{
    asl(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ASL_U6_F(DisasContext *dc, arg_ASL_U6_F *a)
{
    asl(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASL_S12(DisasContext *dc, arg_ASL_S12 *a)
{
    asl(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ASL_CC(DisasContext *dc, arg_ASL_CC *a)
{
    asl(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ASL_CC_U6(DisasContext *dc, arg_ASL_CC_U6 *a)
{
    asl(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    lsr_simple(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_LSR_U6(DisasContext *dc, arg_LSR_U6 *a)
{
    lsr_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_LSR_F(DisasContext *dc, arg_LSR_F *a)
{
    lsr(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_LSR_U6_F(DisasContext *dc, arg_LSR_U6_F *a)
{
    lsr(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_LSR_S12(DisasContext *dc, arg_LSR_S12 *a)
{
    lsr(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_LSR_CC(DisasContext *dc, arg_LSR_CC *a)
{
    lsr(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_LSR_CC_U6(DisasContext *dc, arg_LSR_CC_U6 *a)
{
    lsr(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    asr_simple(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ASR_U6(DisasContext *dc, arg_ASR_U6 *a)
{
    asr_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASR_F(DisasContext *dc, arg_ASR_F *a)
{
    asr(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ASR_U6_F(DisasContext *dc, arg_ASR_U6_F *a)
{
    asr(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASR_S12(DisasContext *dc, arg_ASR_S12 *a)
{
    asr(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ASR_S_U3(DisasContext *dc, arg_ASR_S_U3 *a)
{
    tcg_gen_sari_i32(cpu_regs[arc_reduced_regs[a->c]], cpu_regs[arc_reduced_regs[a->b]], a->u);
    return true;
}

static bool trans_ASR_CC(DisasContext *dc, arg_ASR_CC *a)
{
    asr(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ASR_CC_U6(DisasContext *dc, arg_ASR_CC_U6 *a)
{
    asr(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    ror_simple(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ROR_U6(DisasContext *dc, arg_ROR_U6 *a)
{
    ror_simple(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ROR_F(DisasContext *dc, arg_ROR_F *a)
{
    ror(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ROR_U6_F(DisasContext *dc, arg_ROR_U6_F *a)
{
    ror(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ROR_S12(DisasContext *dc, arg_ROR_S12 *a)
{
    ror(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ROR_CC(DisasContext *dc, arg_ROR_CC *a)
{
    ror(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ROR_CC_U6(DisasContext *dc, arg_ROR_CC_U6 *a)
{
    ror(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_CMP(DisasContext *dc, arg_CMP *a)
{
    cmp(cpu_regs[a->b], cpu_regs[a->c]);
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
    cmp(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_CMP_CC_U6(DisasContext *dc, arg_CMP_CC_U6 *a)
{
    cmp(cpu_regs[a->b], tcg_constant_i32(a->u));
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
    not_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_NOT_U6(DisasContext *dc, arg_NOT_U6 *a)
{
    not_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_NOT_S(DisasContext *dc, arg_NOT_S *a)
{
    tcg_gen_not_i32(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
} 

static bool trans_AEX(DisasContext *dc, arg_AEX *a)
{
    aex(cpu_regs[a->c], cpu_regs[a->b]);
    return true;
}

static bool trans_AEX_U6(DisasContext *dc, arg_AEX_U6 *a)
{
    aex(tcg_constant_i32(a->u), cpu_regs[a->b]);
    return true;
}

static bool trans_AEX_S12(DisasContext *dc, arg_AEX_S12 *a)
{
    aex(tcg_constant_i32(a->s), cpu_regs[a->b]);
    return true;
}

static bool trans_AEX_CC(DisasContext *dc, arg_AEX_CC *a)
{
    aex(cpu_regs[a->c], cpu_regs[a->b]);
    return true;
}

static bool trans_AEX_CC_U6(DisasContext *dc, arg_AEX_CC_U6 *a)
{
    aex(tcg_constant_i32(a->u), cpu_regs[a->b]);
    return true;
}

static bool trans_ABS(DisasContext *dc, arg_ABS *a)
{
    abs_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ABS_U6(DisasContext *dc, arg_ABS_U6 *a)
{
    abs_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ABS_S(DisasContext *dc, arg_ABS_S *a)
{
    tcg_gen_abs_i32(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_ADC(DisasContext *dc, arg_ADC *a)
{
    adc(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADC_U6(DisasContext *dc, arg_ADC_U6 *a)
{
    adc(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ADC_S12(DisasContext *dc, arg_ADC_S12 *a)
{
    adc(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_ADC_CC(DisasContext *dc, arg_ADC_CC *a)
{
    adc(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ADC_CC_U6(DisasContext *dc, arg_ADC_CC_U6 *a)
{
    adc(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ASR16(DisasContext *dc, arg_ASR16 *a)
{
    asr_fixed(cpu_regs[a->b], cpu_regs[a->c], 16, a->f);
    return true;
}

static bool trans_ASR16_U6(DisasContext *dc, arg_ASR16_U6 *a)
{
    asr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 16, a->f);
    return true;
}

static bool trans_ASR8(DisasContext *dc, arg_ASR8 *a)
{
    asr_fixed(cpu_regs[a->b], cpu_regs[a->c], 8, a->f);
    return true;
}

static bool trans_ASR8_U6(DisasContext *dc, arg_ASR8_U6 *a)
{
    asr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 8, a->f);
    return true;
}

static bool trans_LSR16(DisasContext *dc, arg_LSR16 *a)
{
    lsr_fixed(cpu_regs[a->b], cpu_regs[a->c], 16, a->f);
    return true;
}

static bool trans_LSR16_U6(DisasContext *dc, arg_LSR16_U6 *a)
{
    lsr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 16, a->f);
    return true;
}

static bool trans_LSR8(DisasContext *dc, arg_LSR8 *a)
{
    lsr_fixed(cpu_regs[a->b], cpu_regs[a->c], 8, a->f);
    return true;
}

static bool trans_LSR8_U6(DisasContext *dc, arg_LSR8_U6 *a)
{
    lsr_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 8, a->f);
    return true;
}

static bool trans_LSL16(DisasContext *dc, arg_LSL16 *a)
{
    lsl_fixed(cpu_regs[a->b], cpu_regs[a->c], 16, a->f);
    return true;
}

static bool trans_LSL16_U6(DisasContext *dc, arg_LSL16_U6 *a)
{
    lsl_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 16, a->f);
    return true;
}

static bool trans_LSL8(DisasContext *dc, arg_LSL8 *a)
{
    lsl_fixed(cpu_regs[a->b], cpu_regs[a->c], 8, a->f);
    return true;
}

static bool trans_LSL8_U6(DisasContext *dc, arg_LSL8_U6 *a)
{
    lsl_fixed(cpu_regs[a->b], tcg_constant_i32(a->u), 8, a->f);
    return true;
}

static bool trans_ROR8(DisasContext *dc, arg_ROR8 *a)
{
    ror8_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ROR8_U6(DisasContext *dc, arg_ROR8_U6 *a)
{
    ror8_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_ROL(DisasContext *dc, arg_ROL *a)
{
    rol(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_ROL_U6(DisasContext *dc, arg_ROL_U6 *a)
{
    rol(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MAX(DisasContext *dc, arg_MAX *a)
{
    max_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MAX_U6(DisasContext *dc, arg_MAX_U6 *a)
{
    max_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MAX_S12(DisasContext *dc, arg_MAX_S12 *a)
{
    max_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MAX_CC(DisasContext *dc, arg_MAX_CC *a)
{
    max_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MAX_CC_U6(DisasContext *dc, arg_MAX_CC_U6 *a)
{
    max_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MIN(DisasContext *dc, arg_MIN *a)
{
    min_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MIN_U6(DisasContext *dc, arg_MIN_U6 *a)
{
    min_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MIN_S12(DisasContext *dc, arg_MIN_S12 *a)
{
    min_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MIN_CC(DisasContext *dc, arg_MIN_CC *a)
{
    min_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MIN_CC_U6(DisasContext *dc, arg_MIN_CC_U6 *a)
{
    min_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SEXB(DisasContext *dc, arg_SEXB *a)
{
    sexb_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
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
    sexh_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
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
    extb_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
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
    exth_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
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
    rcmp_op(cpu_regs[a->b], cpu_regs[a->c]);
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
    rcmp_op(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_RCMP_CC_U6(DisasContext *dc, arg_rcmp_cc_u6 *a)
{
    rcmp_op(cpu_regs[a->b], tcg_constant_i32(a->u));
    return true;
}

static bool trans_SBC(DisasContext *dc, arg_SBC *a)
{
    sbc_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SBC_U6(DisasContext *dc, arg_SBC_U6 *a)
{
    sbc_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SBC_S12(DisasContext *dc, arg_SBC_S12 *a)
{
    sbc_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_SBC_CC(DisasContext *dc, arg_SBC_CC *a)
{
    sbc_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SBC_CC_U6(DisasContext *dc, arg_SBC_CC_U6 *a)
{
    sbc_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BIC(DisasContext *dc, arg_BIC *a)
{
    bic_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BIC_U6(DisasContext *dc, arg_BIC_U6 *a)
{
    bic_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BIC_S12(DisasContext *dc, arg_BIC_S12 *a)
{
    bic_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BIC_CC(DisasContext *dc, arg_BIC_CC *a)
{
    bic_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BIC_CC_U6(DisasContext *dc, arg_BIC_CC_U6 *a)
{
    bic_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BIC_S(DisasContext *dc, arg_BIC_S *a)
{
    tcg_gen_andc_i32(cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->b]], cpu_regs[arc_reduced_regs[a->c]]);
    return true;
}

static bool trans_TST(DisasContext *dc, arg_TST *a)
{
    tst_op(cpu_regs[a->b], cpu_regs[a->c]);
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
    tst_op(cpu_regs[a->b], cpu_regs[a->c]);
    return true;
}

static bool trans_TST_CC_U6(DisasContext *dc, arg_TST_CC_U6 *a)
{
    tst_op(cpu_regs[a->b], tcg_constant_i32(a->u));
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
    btst_op(cpu_regs[a->b], cpu_regs[a->c]);
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
    btst_op(cpu_regs[a->b], cpu_regs[a->c]);
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
    tcg_gen_and_i32(tmp, cpu_regs[a->b], mask);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_zf, tmp, 0);
    tcg_gen_shri_i32(cpu_nf, tmp, 31);
    return true;
}

static bool trans_BSET(DisasContext *dc, arg_BSET *a)
{
    bset_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BSET_U6(DisasContext *dc, arg_BSET_U6 *a)
{
    bset_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BSET_S12(DisasContext *dc, arg_BSET_S12 *a)
{
    bset_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BSET_CC(DisasContext *dc, arg_BSET_CC *a)
{
    bset_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BSET_CC_U6(DisasContext *dc, arg_BSET_CC_U6 *a)
{
    bset_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    bclr_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BCLR_U6(DisasContext *dc, arg_BCLR_U6 *a)
{
    bclr_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BCLR_S12(DisasContext *dc, arg_BCLR_S12 *a)
{
    bclr_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BCLR_CC(DisasContext *dc, arg_BCLR_CC *a)
{
    bclr_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BCLR_CC_U6(DisasContext *dc, arg_BCLR_CC_U6 *a)
{
    bclr_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    bxor_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BXOR_U6(DisasContext *dc, arg_BXOR_U6 *a)
{
    bxor_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BXOR_S12(DisasContext *dc, arg_BXOR_S12 *a)
{
    bxor_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BXOR_CC(DisasContext *dc, arg_BXOR_CC *a)
{
    bxor_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BXOR_CC_U6(DisasContext *dc, arg_BXOR_CC_U6 *a)
{
    bxor_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BMSK(DisasContext *dc, arg_BMSK *a)
{
    bmsk_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BMSK_U6(DisasContext *dc, arg_BMSK_U6 *a)
{
    bmsk_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_BMSK_S12(DisasContext *dc, arg_BMSK_S12 *a)
{
    bmsk_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_BMSK_CC(DisasContext *dc, arg_BMSK_CC *a)
{
    bmsk_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_BMSK_CC_U6(DisasContext *dc, arg_BMSK_CC_U6 *a)
{
    bmsk_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
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
    swap_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SWAP_U6(DisasContext *dc, arg_SWAP_U6 *a)
{
    swap_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SWAPE(DisasContext *dc, arg_SWAPE *a)
{
    swape_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_SWAPE_U6(DisasContext *dc, arg_SWAPE_U6 *a)
{
    swape_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RLC(DisasContext *dc, arg_RLC *a)
{
    rlc_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_RLC_U6(DisasContext *dc, arg_RLC_U6 *a)
{
    rlc_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RRC(DisasContext *dc, arg_RRC *a)
{
    rrc_op(cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_RRC_U6(DisasContext *dc, arg_RRC_U6 *a)
{
    rrc_op(cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYM(DisasContext *dc, arg_MPYM *a)
{
    mpym_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPYM_U6(DisasContext *dc, arg_MPYM_U6 *a)
{
    mpym_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYM_S12(DisasContext *dc, arg_MPYM_S12 *a)
{
    mpym_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYM_CC(DisasContext *dc, arg_MPYM_CC *a)
{
    mpym_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPYM_CC_U6(DisasContext *dc, arg_MPYM_CC_U6 *a)
{
    mpym_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYMU(DisasContext *dc, arg_MPYMU *a)
{
    mpymu_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPYMU_U6(DisasContext *dc, arg_MPYMU_U6 *a)
{
    mpymu_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYMU_S12(DisasContext *dc, arg_MPYMU_S12 *a)
{
    mpymu_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYMU_CC(DisasContext *dc, arg_MPYMU_CC *a)
{
    mpymu_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPYMU_CC_U6(DisasContext *dc, arg_MPYMU_CC_U6 *a)
{
    mpymu_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYU(DisasContext *dc, arg_MPYU *a)
{
    mpyu_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPYU_U6(DisasContext *dc, arg_MPYU_U6 *a)
{
    mpyu_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_MPYU_S12(DisasContext *dc, arg_MPYU_S12 *a)
{
    mpyu_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_MPYU_CC(DisasContext *dc, arg_MPYU_CC *a)
{
    mpyu_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_MPYU_CC_U6(DisasContext *dc, arg_MPYU_CC_U6 *a)
{
    mpyu_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RSUB(DisasContext *dc, arg_RSUB *a)
{
    rsub_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_RSUB_U6(DisasContext *dc, arg_RSUB_U6 *a)
{
    rsub_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_RSUB_S12(DisasContext *dc, arg_RSUB_S12 *a)
{
    rsub_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->f);
    return true;
}

static bool trans_RSUB_CC(DisasContext *dc, arg_RSUB_CC *a)
{
    rsub_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->f);
    return true;
}

static bool trans_RSUB_CC_U6(DisasContext *dc, arg_RSUB_CC_U6 *a)
{
    rsub_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->f);
    return true;
}

static bool trans_SET(DisasContext *dc, arg_SET *a)
{
    set_op(cpu_regs[a->a], cpu_regs[a->b], cpu_regs[a->c], a->i, a->f);
    return true;
}

static bool trans_SET_U6(DisasContext *dc, arg_SET_U6 *a)
{
    set_op(cpu_regs[a->a], cpu_regs[a->b], tcg_constant_i32(a->u), a->i, a->f);
    return true;
}

static bool trans_SET_S12(DisasContext *dc, arg_SET_S12 *a)
{
    set_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->s), a->i, a->f);
    return true;
}

static bool trans_SET_CC(DisasContext *dc, arg_SET_CC *a)
{
    set_op(cpu_regs[a->b], cpu_regs[a->b], cpu_regs[a->c], a->i, a->f);
    return true;
}

static bool trans_SET_CC_U6(DisasContext *dc, arg_SET_CC_U6 *a)
{
    set_op(cpu_regs[a->b], cpu_regs[a->b], tcg_constant_i32(a->u), a->i, a->f);
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
        if ((extract32(insn, 27, 5) == 0x04 || extract32(insn, 27, 5) == 0x05) && extract32(insn, 22, 2) == 3) {
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