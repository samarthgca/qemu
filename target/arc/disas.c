#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "qemu/bitops.h"
#include "cpu.h"

typedef disassemble_info DisasContext;

#include "decode-insns.c.inc"

#define output(mnemonic, format, ...) \
    (info->fprintf_func(info->stream, "%-9s " format, \
                        mnemonic, ##__VA_ARGS__))

static bool trans_ADD(disassemble_info *info, arg_ADD *a)
{   
    output("add", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_ADD_U6(disassemble_info *info, arg_ADD_U6 *a)
{   
    output("add_u6", "r%d, %d", a->a, a->u);
    return true;
}

static bool trans_ADD_S12(disassemble_info *info, arg_ADD_S12 *a)
{
    output("add_s12", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_ADD_CC_F(DisasContext *info, arg_ADD_CC_F *a)
{
    output("add_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ADD_CC_F_U6(DisasContext *info, arg_ADD_CC_F_U6 *a)
{
    output("add_cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ADD1(disassemble_info *info, arg_ADD1 *a)
{
    output("add1", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_ADD1_U6(disassemble_info *info, arg_ADD1_U6 *a)
{
    output("add1", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_ADD1_S12(disassemble_info *info, arg_ADD1_S12 *a)
{
    output("add1", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_ADD1_CC_F(DisasContext *info, arg_ADD1_CC_F *a)
{
    output("add1_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ADD1_CC_F_U6(DisasContext *info, arg_ADD1_CC_F_U6 *a)
{
    output("add1_cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ADD2(disassemble_info *info, arg_ADD2 *a)
{
    output("add2", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_ADD2_U6(disassemble_info *info, arg_ADD2_U6 *a)
{
    output("add2", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_ADD2_S12(disassemble_info *info, arg_ADD2_S12 *a)
{
    output("add2", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_ADD2_CC_F(DisasContext *info, arg_ADD2_CC_F *a)
{
    output("add2_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ADD2_CC_F_U6(DisasContext *info, arg_ADD2_CC_F_U6 *a)
{
    output("add2_cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ADD3(disassemble_info *info, arg_ADD3 *a)
{
    output("add3", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_ADD3_U6(disassemble_info *info, arg_ADD3_U6 *a)
{
    output("add3", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_ADD3_S12(disassemble_info *info, arg_ADD3_S12 *a)
{
    output("add3", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
 }

static bool trans_ADD3_CC_F(DisasContext *info, arg_ADD3_CC_F *a)
{
    output("add3_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ADD3_CC_F_U6(DisasContext *info, arg_ADD3_CC_F_U6 *a)
{
    output("add3_cc", "r%d, %d", a->b, a->u);
    return true;
}


static bool trans_MOV(disassemble_info *info, arg_MOV *a)
{   
    output("mov", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MOV_U6(disassemble_info *info, arg_MOV_U6 *a)
{   
    output("mov_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MOV_S12(disassemble_info *info, arg_MOV_S12 *a)
{   
    output("mov_s12", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_MOV_CC_F(disassemble_info *info, arg_MOV_CC_F *a)
{
    output("mov.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MOV_CC_F_U6(disassemble_info *info, arg_MOV_CC_F_U6 *a)
{
    output("mov.ccu", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SUB(disassemble_info *info, arg_SUB *a)
{   
    output("sub", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_SUB_u6(disassemble_info *info, arg_SUB_u6 *a)
{   
    output("sub", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_SUB_s12(disassemble_info *info, arg_SUB_s12 *a)
{   
    output("sub", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_SUB_CC(DisasContext *info, arg_SUB_CC *a)
{
    output("sub_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SUB_CC_U6(DisasContext *info, arg_SUB_CC_U6 *a)
{
    output("sub_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SUB1(disassemble_info *info, arg_SUB1 *a)
{
    output("sub1", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_SUB1_U6(disassemble_info *info, arg_SUB1_U6 *a)
{
    output("sub1", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_SUB1_S12(disassemble_info *info, arg_SUB1_S12 *a)
{
    output("sub1", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_SUB1_CC(DisasContext *info, arg_SUB1_CC *a)
{
    output("sub1_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SUB1_CC_U6(DisasContext *info, arg_SUB1_CC_U6 *a)
{
    output("sub1_cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SUB2(disassemble_info *info, arg_SUB2 *a)
{
    output("sub2", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_SUB2_U6(disassemble_info *info, arg_SUB2_U6 *a)
{
    output("sub2", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_SUB2_S12(disassemble_info *info, arg_SUB2_S12 *a)
{
    output("sub2", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_SUB2_CC(DisasContext *info, arg_SUB2_CC *a)
{
    output("sub2_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SUB2_CC_U6(DisasContext *info, arg_SUB2_CC_U6 *a)
{
    output("sub2_cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SUB3(disassemble_info *info, arg_SUB3 *a)
{
    output("sub3", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_SUB3_U6(disassemble_info *info, arg_SUB3_U6 *a)
{
    output("sub3", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_SUB3_S12(disassemble_info *info, arg_SUB3_S12 *a)
{
    output("sub3", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_SUB3_CC(DisasContext *info, arg_SUB3_CC *a)
{
    output("sub3_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SUB3_CC_U6(DisasContext *info, arg_SUB3_CC_U6 *a)
{
    output("sub3_cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPY(disassemble_info *info, arg_MPY *a)
{   
    output("mpy", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPY_u6(disassemble_info *info, arg_MPY_u6 *a)
{   
    output("mpy", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPY_s12(disassemble_info *info, arg_MPY_s12 *a)
{   
    output("mpy", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPY_CC_F(disassemble_info *info, arg_MPY_CC_F *a)
{
    output("mpy.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPY_CC_F_U6(disassemble_info *info, arg_MPY_CC_F_U6 *a)
{
    output("mpy.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_FLAG_U6(disassemble_info *info, arg_FLAG_U6 *a)
{   
    output("flag_u6", "%d", a->u);
    return true;
}

static bool trans_AND(disassemble_info *info, arg_AND *a)
{
    output("and", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_AND_U6(disassemble_info *info, arg_AND_U6 *a)
{
    output("and_u6", "r%d, %d", a->a, a->u);
    return true;
}

static bool trans_AND_S12(disassemble_info *info, arg_AND_S12 *a)
{
    output("and_s12", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_AND_CC(DisasContext *info, arg_AND_CC *a)
{
    output("and_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_AND_CC_U6(DisasContext *info, arg_AND_CC_U6 *a)
{
    output("and_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_OR(disassemble_info *info, arg_OR *a)
{
    output("or", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_OR_U6(disassemble_info *info, arg_OR_U6 *a)
{
    output("or_u6", "r%d, %d", a->a, a->u);
    return true;
}

static bool trans_OR_S12(disassemble_info *info, arg_OR_S12 *a)
{
    output("or_s12", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_OR_CC(DisasContext *info, arg_OR_CC *a)
{
    output("or_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_OR_CC_U6(DisasContext *info, arg_OR_CC_U6 *a)
{
    output("or_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_XOR(disassemble_info *info, arg_XOR *a)
{
    output("xor", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_XOR_U6(disassemble_info *info, arg_XOR_U6 *a)
{
    output("xor_u6", "r%d, %d", a->a, a->u);
    return true;
}

static bool trans_XOR_S12(disassemble_info *info, arg_XOR_S12 *a)
{
    output("xor_s12", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_XOR_CC(DisasContext *info, arg_XOR_CC *a)
{
    output("xor_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_XOR_CC_U6(DisasContext *info, arg_XOR_CC_U6 *a)
{
    output("xor_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ASL(DisasContext *info, arg_ASL *a)
{
    output("ASL", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ASL_U6(DisasContext *info, arg_ASL_U6 *a)
{
    output("ASL", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ASL_F(disassemble_info *info, arg_ASL_F *a)
{
    output("asl", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_ASL_U6_F(disassemble_info *info, arg_ASL_U6_F *a)
{
    output("asl", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_LSR_F(disassemble_info *info, arg_LSR_F *a)
{
    output("lsr", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_LSR_U6_F(disassemble_info *info, arg_LSR_U6_F *a)
{
    output("lsr", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_ASR_F(disassemble_info *info, arg_ASR_F *a)
{
    output("asr", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}
                        
static bool trans_ASR_U6_F(disassemble_info *info, arg_ASR_U6_F *a)
{
    output("asr", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_ASL_S12(DisasContext *info, arg_ASL_S12 *a)
{
    output("ASL", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_ASL_CC(DisasContext *info, arg_ASL_CC *a)
{
    output("asl_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ASL_CC_U6(DisasContext *info, arg_ASL_CC_U6 *a)
{
    output("asl_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_LSR(DisasContext *info, arg_LSR *a)
{
    output("LSR", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_LSR_U6(DisasContext *info, arg_LSR_U6 *a)
{
    output("LSR", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_LSR_S12(DisasContext *info, arg_LSR_S12 *a)
{
    output("LSR", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_LSR_CC(DisasContext *info, arg_LSR_CC *a)
{
    output("lsr_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_LSR_CC_U6(DisasContext *info, arg_LSR_CC_U6 *a)
{
    output("lsr_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ASR(DisasContext *info, arg_ASR *a)
{
    output("ASR", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ASR_U6(DisasContext *info, arg_ASR_U6 *a)
{
    output("ASR", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ASR_S12(disassemble_info *info, arg_ASR_S12 *a)
{
    output("asr_s12", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_ASR_CC(DisasContext *info, arg_ASR_CC *a)
{
    output("asr_cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ASR_CC_U6(DisasContext *info, arg_ASR_CC_U6 *a)
{
    output("asr_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ROR(disassemble_info *info, arg_ROR *a)
{
    output("ror", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ROR_U6(disassemble_info *info, arg_ROR_U6 *a)
{
    output("ror", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ROR_F(disassemble_info *info, arg_ROR_F *a)
{
    output("ror", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_ROR_U6_F(disassemble_info *info, arg_ROR_U6_F *a)
{
    output("ror_u6", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_ROR_S12(disassemble_info *info, arg_ROR_S12 *a)
{
    output("ror_s12", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_ROR_CC(DisasContext *info, arg_ROR_CC *a)
{
    output("ror_cc", "r%d, r%d", a->b, a->c);
    return true;
}

  static bool trans_ROR_CC_U6(DisasContext *info, arg_ROR_CC_U6 *a)
{
    output("ror_cc_u6", "r%d, %d", a->b, a->u);
    return true;
}



static bool trans_CMP(disassemble_info *info, arg_CMP *a)
{   
    output("CMP", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_CMP_U6(disassemble_info *info, arg_CMP_U6 *a)
{
    output("CMP", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_CMP_S12(disassemble_info *info, arg_CMP_S12 *a)
{
    output("CMP", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_CMP_CC(disassemble_info *info, arg_CMP_CC *a)
{
    output("CMP.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_CMP_CC_U6(disassemble_info *info, arg_CMP_CC_U6 *a)
{
    output("CMP.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BRANCH(disassemble_info *info, arg_BRANCH *a)
{
    output("branch", "%d", a->sb);
    return true;
}

static bool trans_BRANCH_C(disassemble_info *info, arg_BRANCH_C *a)
{
     output("bbit0", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BRANCH_C_U6(disassemble_info *info, arg_BRANCH_C_U6 *a)
{
    output("bbit0", "r%d, %d, %d", a->b, a->u, a->sbc);
    return true;
}

static bool trans_BRANCH_CC(disassemble_info *info, arg_BRANCH_CC *a)
{
    output("bcc", "%d, %d", a->q, a->sb);
    return true;
}

static bool trans_BRANCH1_C(disassemble_info *info, arg_BRANCH1_C *a)
{
     output("bbit1", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BRANCH1_C_U6(disassemble_info *info, arg_BRANCH1_C_U6 *a)
{
    output("bbit1", "r%d, %d, %d", a->b, a->u, a->sbc);
    return true;
}

static bool trans_NOT(disassemble_info *info, arg_NOT *a)
{
    output("not", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_NOT_U6(disassemble_info *info, arg_NOT_U6 *a)
{
    output("not", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_AEX(disassemble_info *info, arg_AEX *a)
{
    output("aex", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_AEX_U6(disassemble_info *info, arg_AEX_U6 *a)
{
    output("aex", "r%d, [%d]", a->b, a->u);
    return true;
}

static bool trans_AEX_S12(disassemble_info *info, arg_AEX_S12 *a)
{
    output("aex", "r%d, [%d]", a->b, a->s);
    return true;
}

static bool trans_AEX_CC(disassemble_info *info, arg_AEX_CC *a)
{
    output("aex.cc", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_AEX_CC_U6(disassemble_info *info, arg_AEX_CC_U6 *a)
{
    output("aex.cc", "r%d, [%d]", a->b, a->u);
    return true;
}

static bool trans_ABS(disassemble_info *info, arg_ABS *a)
{
    output("abs", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_ABS_U6(disassemble_info *info, arg_ABS_U6 *a)
{
    output("abs", "r%d, [%d]", a->b, a->u);
    return true;
}

static bool trans_ADC(disassemble_info *info, arg_ADC *a)
{
    output("adc", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_ADC_U6(disassemble_info *info, arg_ADC_U6 *a)
{
    output("adc", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_ADC_S12(disassemble_info *info, arg_ADC_S12 *a)
{
    output("adc", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_ADC_CC(disassemble_info *info, arg_ADC_CC *a)
{
    output("adc.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ADC_CC_U6(disassemble_info *info, arg_ADC_CC_U6 *a)
{
    output("adc.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ASR16(disassemble_info *info, arg_ASR16 *a)
{
    output("asr16", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ASR16_U6(disassemble_info *info, arg_ASR16_U6 *a)
{
    output("asr16", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ASR8(disassemble_info *info, arg_ASR8 *a)
{
    output("asr16", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ASR8_U6(disassemble_info *info, arg_ASR8_U6 *a)
{
    output("asr16", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_LSR16(disassemble_info *info, arg_LSR16 *a)
{
    output("lsr16", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_LSR16_U6(disassemble_info *info, arg_LSR16_U6 *a)
{
    output("lsr16", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_LSR8(disassemble_info *info, arg_LSR8 *a)
{
    output("lsr8", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_LSR8_U6(disassemble_info *info, arg_LSR8_U6 *a)
{
    output("lsr8", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_LSL16(disassemble_info *info, arg_LSL16 *a)
{
    output("lsl16", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_LSL16_U6(disassemble_info *info, arg_LSL16_U6 *a)
{
    output("lsl16", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ROR8(disassemble_info *info, arg_ROR8 *a)
{
    output("ror8", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ROR8_U6(disassemble_info *info, arg_ROR8_U6 *a)
{
    output("ror8", "r%d, %d", a->b, a->u);
    return true;
}


static bool trans_LSL8(disassemble_info *info, arg_LSL8 *a)
{
    output("lsl8", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_LSL8_U6(disassemble_info *info, arg_LSL8_U6 *a)
{
    output("lsl8", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ROL(disassemble_info *info, arg_ROL *a)
{
    output("rol8", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ROL_U6(disassemble_info *info, arg_ROL_U6 *a)
{
    output("rol8", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MAX(disassemble_info *info, arg_MAX *a)
{
    output("max", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MAX_U6(disassemble_info *info, arg_MAX_U6 *a)
{
    output("max", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MAX_S12(disassemble_info *info, arg_MAX_S12 *a)
{
    output("max", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MAX_CC(disassemble_info *info, arg_MAX_CC *a)
{
    output("max.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MAX_CC_U6(disassemble_info *info, arg_MAX_CC_U6 *a)
{
    output("max.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MIN(disassemble_info *info, arg_MIN *a)
{
    output("min", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MIN_U6(disassemble_info *info, arg_MIN_U6 *a)
{
    output("min", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MIN_S12(disassemble_info *info, arg_MIN_S12 *a)
{
    output("min", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MIN_CC(disassemble_info *info, arg_MIN_CC *a)
{
    output("min.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MIN_CC_U6(disassemble_info *info, arg_MIN_CC_U6 *a)
{
    output("min.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SEXB(disassemble_info *info, arg_SEXB *a)
{
    output("sexb", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SEXB_U6(disassemble_info *info, arg_SEXB_U6 *a)
{
    output("sexb", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SEXH(disassemble_info *info, arg_SEXH *a)
{
    output("sexh", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SEXH_U6(disassemble_info *info, arg_SEXH_U6 *a)
{
    output("sexh", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_EXTB(disassemble_info *info, arg_EXTB *a)
{
    output("extb", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_EXTB_U6(disassemble_info *info, arg_EXTB_U6 *a)
{
    output("extb", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_EXTH(disassemble_info *info, arg_EXTH *a)
{
    output("exth", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_EXTH_U6(disassemble_info *info, arg_EXTH_U6 *a)
{
    output("exth", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_RCMP(disassemble_info *info, arg_RCMP *a)
{
    output("rcmp", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_RCMP_U6(disassemble_info *info, arg_RCMP_U6 *a)
{
    output("rcmp", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_RCMP_S12(disassemble_info *info, arg_RCMP_S12 *a)
{
    output("rcmp", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_RCMP_CC(disassemble_info *info, arg_RCMP_CC *a)
{
    output("rcmp.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_RCMP_CC_U6(disassemble_info *info, arg_RCMP_CC_U6 *a)
{
    output("rcmp.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SBC(disassemble_info *info, arg_SBC *a)
{
    output("sbc", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_SBC_U6(disassemble_info *info, arg_SBC_U6 *a)
{
    output("sbc", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_SBC_S12(disassemble_info *info, arg_SBC_S12 *a)
{
    output("sbc", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_SBC_CC(disassemble_info *info, arg_SBC_CC *a)
{
    output("sbc.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SBC_CC_U6(disassemble_info *info, arg_SBC_CC_U6 *a)
{
    output("sbc.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BIC(disassemble_info *info, arg_BIC *a)
{
    output("bic", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_BIC_U6(disassemble_info *info, arg_BIC_U6 *a)
{
    output("bic", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_BIC_S12(disassemble_info *info, arg_BIC_S12 *a)
{
    output("bic", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_BIC_CC(disassemble_info *info, arg_BIC_CC *a)
{
    output("bic.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BIC_CC_U6(disassemble_info *info, arg_BIC_CC_U6 *a)
{
    output("bic.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_TST(disassemble_info *info, arg_TST *a)
{
    output("tst", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_TST_U6(disassemble_info *info, arg_TST_U6 *a)
{
    output("tst", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_TST_S12(disassemble_info *info, arg_TST_S12 *a)
{
    output("tst", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_TST_CC(disassemble_info *info, arg_TST_CC *a)
{
    output("tst.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_TST_CC_U6(disassemble_info *info, arg_TST_CC_U6 *a)
{
    output("tst.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BTST(disassemble_info *info, arg_BTST *a)
{
    output("btst", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BTST_U6(disassemble_info *info, arg_BTST_U6 *a)
{
    output("btst", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BTST_S12(disassemble_info *info, arg_BTST_S12 *a)
{
    output("btst", "r%d, %d", a->b, a->s);
    return true;
}

static bool trans_BTST_CC(disassemble_info *info, arg_BTST_CC *a)
{
    output("btst.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BTST_CC_U6(disassemble_info *info, arg_BTST_CC_U6 *a)
{
    output("btst.cc", "r%d, %d", a->b, a->u);
    return true;
}

int print_insn_arc(bfd_vma addr, disassemble_info *info)
{
    bfd_byte buffer[4];
    uint32_t insn;
    int status;
    status = info->read_memory_func(addr, buffer, 4, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }
    insn = (bfd_getl16(buffer) << 16) | bfd_getl16(buffer + 2);
    if (!decode(info, insn)) {
        output(".long", "%#08x", insn);
    }
    return 4;
}