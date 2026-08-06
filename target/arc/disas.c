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

static bool trans_BREQ_C(disassemble_info *info, arg_BREQ_C *a)
{
    output("breq", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BREQ_C_U6(disassemble_info *info, arg_BREQ_C_U6 *a)
{
    output("breq", "r%d, %d, %d", a->b, a->u, a->sbc);
    return true;
}

static bool trans_BRNE_C(disassemble_info *info, arg_BRNE_C *a)
{
    output("brne", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BRNE_C_U6(disassemble_info *info, arg_BRNE_C_U6 *a)
{
    output("brne", "r%d, %d, %d", a->b, a->u, a->sbc);
    return true;
}

static bool trans_BRLT_C(disassemble_info *info, arg_BRLT_C *a)
{
    output("brlt", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BRLT_C_U6(disassemble_info *info, arg_BRLT_C_U6 *a)
{
    output("brlt", "r%d, %d, %d", a->b, a->u, a->sbc);
    return true;
}

static bool trans_BRGE_C(disassemble_info *info, arg_BRGE_C *a)
{
    output("brge", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BRGE_C_U6(disassemble_info *info, arg_BRGE_C_U6 *a)
{
    output("brge", "r%d, %d, %d", a->b, a->u, a->sbc);
    return true;
}

static bool trans_BRLO_C(disassemble_info *info, arg_BRLO_C *a)
{
    output("brlo", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BRLO_C_U6(disassemble_info *info, arg_BRLO_C_U6 *a)
{
    output("brlo", "r%d, %d, %d", a->b, a->u, a->sbc);
    return true;
}

static bool trans_BRHS_C(disassemble_info *info, arg_BRHS_C *a)
{
    output("brhs", "r%d, r%d, %d", a->b, a->c, a->sbc);
    return true;
}

static bool trans_BRHS_C_U6(disassemble_info *info, arg_BRHS_C_U6 *a)
{
    output("brhs", "r%d, %d, %d", a->b, a->u, a->sbc);
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

static bool trans_JCC(disassemble_info *info, arg_JCC *a)
{
    output("j.cc", "[r%d]", a->c);
    return true;
}

static bool trans_JCC_U6(disassemble_info *info, arg_JCC_U6 *a)
{
    output("j.cc", "%d", a->u);
    return true;
}

static bool trans_JCCD(disassemble_info *info, arg_JCCD *a)
{
    output("j.cc.d", "[r%d]", a->c);
    return true;
}

static bool trans_JCCD_U6(disassemble_info *info, arg_JCCD_U6 *a)
{
    output("j.cc.d", "%d", a->u);
    return true;
}

static bool trans_BSET(disassemble_info *info, arg_BSET *a)
{
    output("bset", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_BSET_U6(disassemble_info *info, arg_BSET_U6 *a)
{
    output("bset", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_BSET_S12(disassemble_info *info, arg_BSET_S12 *a)
{
    output("bset", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_BSET_CC(disassemble_info *info, arg_BSET_CC *a)
{
    output("bset.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BSET_CC_U6(disassemble_info *info, arg_BSET_CC_U6 *a)
{
    output("bset.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BCLR(disassemble_info *info, arg_BCLR *a)
{
    output("bclr", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_BCLR_U6(disassemble_info *info, arg_BCLR_U6 *a)
{
    output("bclr", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_BCLR_S12(disassemble_info *info, arg_BCLR_S12 *a)
{
    output("bclr", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_BCLR_CC(disassemble_info *info, arg_BCLR_CC *a)
{
    output("bclr.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BCLR_CC_U6(disassemble_info *info, arg_BCLR_CC_U6 *a)
{
    output("bclr.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BXOR(disassemble_info *info, arg_BXOR *a)
{
    output("bxor", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_BXOR_U6(disassemble_info *info, arg_BXOR_U6 *a)
{
    output("bxor", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_BXOR_S12(disassemble_info *info, arg_BXOR_S12 *a)
{
    output("bxor", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_BXOR_CC(disassemble_info *info, arg_BXOR_CC *a)
{
    output("bxor.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BXOR_CC_U6(disassemble_info *info, arg_BXOR_CC_U6 *a)
{
    output("bxor.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BMSK(disassemble_info *info, arg_BMSK *a)
{
    output("bmsk", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_BMSK_U6(disassemble_info *info, arg_BMSK_U6 *a)
{
    output("bmsk", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_BMSK_S12(disassemble_info *info, arg_BMSK_S12 *a)
{
    output("bmsk", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_BMSK_CC(disassemble_info *info, arg_BMSK_CC *a)
{
    output("bmsk.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BMSK_CC_U6(disassemble_info *info, arg_BMSK_CC_U6 *a)
{
    output("bmsk.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SWAP(disassemble_info *info, arg_SWAP *a)
{
    output("swap", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SWAP_U6(disassemble_info *info, arg_SWAP_U6 *a)
{
    output("swap", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SWAPE(disassemble_info *info, arg_SWAPE *a)
{
    output("swape", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_SWAPE_U6(disassemble_info *info, arg_SWAPE_U6 *a)
{
    output("swape", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_RLC(disassemble_info *info, arg_RLC *a)
{
    output("rlc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_RLC_U6(disassemble_info *info, arg_RLC_U6 *a)
{
    output("rlc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_RRC(disassemble_info *info, arg_RRC *a)
{
    output("rrc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_RRC_U6(disassemble_info *info, arg_RRC_U6 *a)
{
    output("rrc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPYM(disassemble_info *info, arg_MPYM *a)
{
    output("mpym", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPYM_U6(disassemble_info *info, arg_MPYM_U6 *a)
{
    output("mpym", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPYM_S12(disassemble_info *info, arg_MPYM_S12 *a)
{
    output("mpym", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPYM_CC(disassemble_info *info, arg_MPYM_CC *a)
{
    output("mpym.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPYM_CC_U6(disassemble_info *info, arg_MPYM_CC_U6 *a)
{
    output("mpym.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPYMU(disassemble_info *info, arg_MPYMU *a)
{
    output("mpymu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPYMU_U6(disassemble_info *info, arg_MPYMU_U6 *a)
{
    output("mpymu", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPYMU_S12(disassemble_info *info, arg_MPYMU_S12 *a)
{
    output("mpymu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPYMU_CC(disassemble_info *info, arg_MPYMU_CC *a)
{
    output("mpymu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPYMU_CC_U6(disassemble_info *info, arg_MPYMU_CC_U6 *a)
{
    output("mpymu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPYU(disassemble_info *info, arg_MPYU *a)
{
    output("mpyu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPYU_U6(disassemble_info *info, arg_MPYU_U6 *a)
{
    output("mpyu", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPYU_S12(disassemble_info *info, arg_MPYU_S12 *a)
{
    output("mpyu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPYU_CC(disassemble_info *info, arg_MPYU_CC *a)
{
    output("mpyu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPYU_CC_U6(disassemble_info *info, arg_MPYU_CC_U6 *a)
{
    output("mpyu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_RSUB(disassemble_info *info, arg_RSUB *a)
{
    output("rsub", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_RSUB_U6(disassemble_info *info, arg_RSUB_U6 *a)
{
    output("rsub", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_RSUB_S12(disassemble_info *info, arg_RSUB_S12 *a)
{
    output("rsub", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_RSUB_CC(disassemble_info *info, arg_RSUB_CC *a)
{
    output("rsub.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_RSUB_CC_U6(disassemble_info *info, arg_RSUB_CC_U6 *a)
{
    output("rsub.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_SET(disassemble_info *info, arg_SET *a)
{
    output("set", "%d, r%d, r%d, r%d", a->i, a->a, a->b, a->c);
    return true;
}

static bool trans_SET_U6(disassemble_info *info, arg_SET_U6 *a)
{
    output("set", "%d, r%d, r%d, %d", a->i, a->a, a->b, a->u);
    return true;
}

static bool trans_SET_S12(disassemble_info *info, arg_SET_S12 *a)
{
    output("set", "%d, r%d, r%d, %d", a->i, a->b, a->b, a->s);
    return true;
}

static bool trans_SET_CC(disassemble_info *info, arg_SET_CC *a)
{
    output("set.cc", "%d, r%d, r%d", a->i, a->b, a->c);
    return true;
}

static bool trans_SET_CC_U6(disassemble_info *info, arg_SET_CC_U6 *a)
{
    output("set.cc", "%d, r%d, %d", a->i, a->b, a->u);
    return true;
}

static bool trans_BMSKN(disassemble_info *info, arg_BMSKN *a)
{
    output("bmskn", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_BMSKN_U6(disassemble_info *info, arg_BMSKN_U6 *a)
{
    output("bmskn", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_BMSKN_S12(disassemble_info *info, arg_BMSKN_S12 *a)
{
    output("bmskn", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_BMSKN_CC(disassemble_info *info, arg_BMSKN_CC *a)
{
    output("bmskn.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_BMSKN_CC_U6(disassemble_info *info, arg_BMSKN_CC_U6 *a)
{
    output("bmskn.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPYW(disassemble_info *info, arg_MPYW *a)
{
    output("mpyw", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPYW_U6(disassemble_info *info, arg_MPYW_U6 *a)
{
    output("mpyw", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPYW_S12(disassemble_info *info, arg_MPYW_S12 *a)
{
    output("mpyw", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPYW_CC(disassemble_info *info, arg_MPYW_CC *a)
{
    output("mpyw.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPYW_CC_U6(disassemble_info *info, arg_MPYW_CC_U6 *a)
{
    output("mpyw.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPYUW(disassemble_info *info, arg_MPYUW *a)
{
    output("mpyuw", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPYUW_U6(disassemble_info *info, arg_MPYUW_U6 *a)
{
    output("mpyuw", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPYUW_S12(disassemble_info *info, arg_MPYUW_S12 *a)
{
    output("mpyuw", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPYUW_CC(disassemble_info *info, arg_MPYUW_CC *a)
{
    output("mpyuw.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPYUW_CC_U6(disassemble_info *info, arg_MPYUW_CC_U6 *a)
{
    output("mpyuw.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_FFS(disassemble_info *info, arg_FFS *a)
{
    output("ffs", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_FFS_U6(disassemble_info *info, arg_FFS_U6 *a)
{
    output("ffs", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_FLS(disassemble_info *info, arg_FLS *a)
{
    output("fls", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_FLS_U6(disassemble_info *info, arg_FLS_U6 *a)
{
    output("fls", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_NORM(disassemble_info *info, arg_NORM *a)
{
    output("norm", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_NORM_U6(disassemble_info *info, arg_NORM_U6 *a)
{
    output("norm", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_ROL8(disassemble_info *info, arg_ROL8 *a)
{
    output("rol8", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_ROL8_U6(disassemble_info *info, arg_ROL8_U6 *a)
{
    output("rol8", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_FLAG(disassemble_info *info, arg_FLAG *a)
{
    output("flag", "r%d", a->c);
    return true;
}

static bool trans_FLAG_S12(disassemble_info *info, arg_FLAG_S12 *a)
{
    output("flag", "%d", a->s);
    return true;
}

static bool trans_FLAG_CC(disassemble_info *info, arg_FLAG_CC *a)
{
    output("flag.cc", "r%d", a->c);
    return true;
}

static bool trans_FLAG_CC_U6(disassemble_info *info, arg_FLAG_CC_U6 *a)
{
    output("flag.cc", "%d", a->u);
    return true;
}

static bool trans_NORMH(disassemble_info *info, arg_NORMH *a)
{
    output("normh", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_NORMH_U6(disassemble_info *info, arg_NORMH_U6 *a)
{
    output("normh", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_LR(disassemble_info *info, arg_LR *a)
{
    output("lr", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_LR_U6(disassemble_info *info, arg_LR_U6 *a)
{
    output("lr", "r%d, [%d]", a->b, a->u);
    return true;
}

static bool trans_LR_S12(disassemble_info *info, arg_LR_S12 *a)
{
    output("lr", "r%d, [%d]", a->b, a->s);
    return true;
}

static bool trans_SR(disassemble_info *info, arg_SR *a)
{
    output("sr", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_SR_U6(disassemble_info *info, arg_SR_U6 *a)
{
    output("sr", "r%d, [%d]", a->b, a->u);
    return true;
}

static bool trans_SR_S12(disassemble_info *info, arg_SR_S12 *a)
{
    output("sr", "r%d, [%d]", a->b, a->s);
    return true;
}

static bool trans_LD(disassemble_info *info, arg_LD *a)
{
    output("ld", "r%d, [r%d,r%d]", a->a, a->b, a->c);
    return true;
}

static bool trans_LD_S9(disassemble_info *info, arg_LD_S9 *a)
{
    output("ld", "r%d, [r%d,%d]", a->a, a->b, a->s);
    return true;
}

static bool trans_DIV(disassemble_info *info, arg_DIV *a)
{
    output("div", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_DIV_U6(disassemble_info *info, arg_DIV_U6 *a)
{
    output("div", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_DIV_S12(disassemble_info *info, arg_DIV_S12 *a)
{
    output("div", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_DIV_CC(disassemble_info *info, arg_DIV_CC *a)
{
    output("div.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_DIV_CC_U6(disassemble_info *info, arg_DIV_CC_U6 *a)
{
    output("div.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_DIVU(disassemble_info *info, arg_DIVU *a)
{
    output("divu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_REMU(disassemble_info *info, arg_REMU *a)
{
    output("remu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_REMU_S12(disassemble_info *info, arg_REMU_S12 *a)
{
    output("remu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_REMU_CC(disassemble_info *info, arg_REMU_CC *a)
{
    output("remu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_REMU_CC_U6(disassemble_info *info, arg_REMU_CC_U6 *a)
{
    output("remu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_DIVU_U6(disassemble_info *info, arg_DIVU_U6 *a)
{
    output("divu", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_DIVU_S12(disassemble_info *info, arg_DIVU_S12 *a)
{
    output("divu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_DIVU_CC(disassemble_info *info, arg_DIVU_CC *a)
{
    output("divu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_DIVU_CC_U6(disassemble_info *info, arg_DIVU_CC_U6 *a)
{
    output("divu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPYD(disassemble_info *info, arg_MPYD *a)
{
    output("mpyd", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPYD_U6(disassemble_info *info, arg_MPYD_U6 *a)
{
    output("mpyd", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPYD_S12(disassemble_info *info, arg_MPYD_S12 *a)
{
    output("mpyd", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPYD_CC(disassemble_info *info, arg_MPYD_CC *a)
{
    output("mpyd.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPYD_CC_U6(disassemble_info *info, arg_MPYD_CC_U6 *a)
{
    output("mpyd.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_MPYDU(disassemble_info *info, arg_MPYDU *a)
{
    output("mpydu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_MPYDU_U6(disassemble_info *info, arg_MPYDU_U6 *a)
{
    output("mpydu", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_MPYDU_S12(disassemble_info *info, arg_MPYDU_S12 *a)
{
    output("mpydu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_MPYDU_CC(disassemble_info *info, arg_MPYDU_CC *a)
{
    output("mpydu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_MPYDU_CC_U6(disassemble_info *info, arg_MPYDU_CC_U6 *a)
{
    output("mpydu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_DMACH(disassemble_info *info, arg_DMACH *a)
{
    output("dmach", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_DMACH_U6(disassemble_info *info, arg_DMACH_U6 *a)
{
    output("dmach", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_DMACH_S12(disassemble_info *info, arg_DMACH_S12 *a)
{
    output("dmach", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_DMACH_CC(disassemble_info *info, arg_DMACH_CC *a)
{
    output("dmach.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_DMACH_CC_U6(disassemble_info *info, arg_DMACH_CC_U6 *a)
{
    output("dmach.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_DMACHU(disassemble_info *info, arg_DMACHU *a)
{
    output("dmachu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_DMACHU_U6(disassemble_info *info, arg_DMACHU_U6 *a)
{
    output("dmachu", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_DMACHU_S12(disassemble_info *info, arg_DMACHU_S12 *a)
{
    output("dmachu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_DMACHU_CC(disassemble_info *info, arg_DMACHU_CC *a)
{
    output("dmachu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_DMACHU_CC_U6(disassemble_info *info, arg_DMACHU_CC_U6 *a)
{
    output("dmachu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VMPY2H(disassemble_info *info, arg_VMPY2H *a)
{
    output("vmpy2h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VMPY2H_U6(disassemble_info *info, arg_VMPY2H_U6 *a)
{
    output("vmpy2h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VMPY2H_S12(disassemble_info *info, arg_VMPY2H_S12 *a)
{
    output("vmpy2h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VMPY2H_CC(disassemble_info *info, arg_VMPY2H_CC *a)
{
    output("vmpy2h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VMPY2H_CC_U6(disassemble_info *info, arg_VMPY2H_CC_U6 *a)
{
    output("vmpy2h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VMPY2HU(disassemble_info *info, arg_VMPY2HU *a)
{
    output("vmpy2hu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VMPY2HU_U6(disassemble_info *info, arg_VMPY2HU_U6 *a)
{
    output("vmpy2hu", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VMPY2HU_S12(disassemble_info *info, arg_VMPY2HU_S12 *a)
{
    output("vmpy2hu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VMPY2HU_CC(disassemble_info *info, arg_VMPY2HU_CC *a)
{
    output("vmpy2hu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VMPY2HU_CC_U6(disassemble_info *info, arg_VMPY2HU_CC_U6 *a)
{
    output("vmpy2hu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VSUB2(disassemble_info *info, arg_VSUB2 *a)
{
    output("vsub2", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VSUB2_U6(disassemble_info *info, arg_VSUB2_U6 *a)
{
    output("vsub2", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VSUB2_S12(disassemble_info *info, arg_VSUB2_S12 *a)
{
    output("vsub2", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VSUB2_CC(disassemble_info *info, arg_VSUB2_CC *a)
{
    output("vsub2.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VSUB2_CC_U6(disassemble_info *info, arg_VSUB2_CC_U6 *a)
{
    output("vsub2.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VSUB2H(disassemble_info *info, arg_VSUB2H *a)
{
    output("vsub2h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VSUB2H_U6(disassemble_info *info, arg_VSUB2H_U6 *a)
{
    output("vsub2h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VSUB2H_S12(disassemble_info *info, arg_VSUB2H_S12 *a)
{
    output("vsub2h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VSUB2H_CC(disassemble_info *info, arg_VSUB2H_CC *a)
{
    output("vsub2h.cc", "r%d, r%d", a->b, a->c);
    return true;
}                                              

static bool trans_VSUB2H_CC_U6(disassemble_info *info, arg_VSUB2H_CC_U6 *a)
{
    output("vsub2h.cc", "r%d, %d", a->b, a->u);
    return true;
}
    
static bool trans_VADD2(disassemble_info *info, arg_VADD2 *a)
{
    output("vadd2", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}
    
static bool trans_VADD2_U6(disassemble_info *info, arg_VADD2_U6 *a)
{
    output("vadd2", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VADD2_S12(disassemble_info *info, arg_VADD2_S12 *a)
{
    output("vadd2", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VADD2_CC(disassemble_info *info, arg_VADD2_CC *a)
{
    output("vadd2.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VADD2_CC_U6(disassemble_info *info, arg_VADD2_CC_U6 *a)
{
    output("vadd2.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VADD2H(disassemble_info *info, arg_VADD2H *a)
{
    output("vadd2h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VADD2H_U6(disassemble_info *info, arg_VADD2H_U6 *a)
{
    output("vadd2h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VADD2H_S12(disassemble_info *info, arg_VADD2H_S12 *a)
{
    output("vadd2h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VADD2H_CC(disassemble_info *info, arg_VADD2H_CC *a)
{
    output("vadd2h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VADD2H_CC_U6(disassemble_info *info, arg_VADD2H_CC_U6 *a)
{
    output("vadd2h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VADD4H(disassemble_info *info, arg_VADD4H *a)
{
    output("vadd4h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VADD4H_U6(disassemble_info *info, arg_VADD4H_U6 *a)
{
    output("vadd4h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VADD4H_S12(disassemble_info *info, arg_VADD4H_S12 *a)
{
    output("vadd4h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VADD4H_CC(disassemble_info *info, arg_VADD4H_CC *a)
{
    output("vadd4h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VADD4H_CC_U6(disassemble_info *info, arg_VADD4H_CC_U6 *a)
{
    output("vadd4h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VADDSUB(disassemble_info *info, arg_VADDSUB *a)
{
    output("vaddsub", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VADDSUB_U6(disassemble_info *info, arg_VADDSUB_U6 *a)
{
    output("vaddsub", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VADDSUB_S12(disassemble_info *info, arg_VADDSUB_S12 *a)
{
    output("vaddsub", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VADDSUB_CC(disassemble_info *info, arg_VADDSUB_CC *a)
{
    output("vaddsub.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VADDSUB_CC_U6(disassemble_info *info, arg_VADDSUB_CC_U6 *a)
{
    output("vaddsub.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VADDSUB2H(disassemble_info *info, arg_VADDSUB2H *a)
{
    output("vaddsub2h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VADDSUB2H_U6(disassemble_info *info, arg_VADDSUB2H_U6 *a)
{
    output("vaddsub2h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VADDSUB2H_S12(disassemble_info *info, arg_VADDSUB2H_S12 *a)
{
    output("vaddsub2h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VADDSUB2H_CC(disassemble_info *info, arg_VADDSUB2H_CC *a)
{
    output("vaddsub2h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VADDSUB2H_CC_U6(disassemble_info *info, arg_VADDSUB2H_CC_U6 *a)
{
    output("vaddsub2h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VADDSUB4H(disassemble_info *info, arg_VADDSUB4H *a)
{
    output("vaddsub4h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VADDSUB4H_U6(disassemble_info *info, arg_VADDSUB4H_U6 *a)
{
    output("vaddsub4h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VADDSUB4H_S12(disassemble_info *info, arg_VADDSUB4H_S12 *a)
{
    output("vaddsub4h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VADDSUB4H_CC(disassemble_info *info, arg_VADDSUB4H_CC *a)
{
    output("vaddsub4h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VADDSUB4H_CC_U6(disassemble_info *info, arg_VADDSUB4H_CC_U6 *a)
{
    output("vaddsub4h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VSUBADD(disassemble_info *info, arg_VSUBADD *a)
{
    output("vsubadd", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VSUBADD_U6(disassemble_info *info, arg_VSUBADD_U6 *a)
{
    output("vsubadd", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VSUBADD_S12(disassemble_info *info, arg_VSUBADD_S12 *a)
{
    output("vsubadd", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VSUBADD_CC(disassemble_info *info, arg_VSUBADD_CC *a)
{
    output("vsubadd.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VSUBADD_CC_U6(disassemble_info *info, arg_VSUBADD_CC_U6 *a)
{
    output("vsubadd.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VSUBADD2H(disassemble_info *info, arg_VSUBADD2H *a)
{
    output("vsubadd2h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VSUBADD2H_U6(disassemble_info *info, arg_VSUBADD2H_U6 *a)
{
    output("vsubadd2h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VSUBADD2H_S12(disassemble_info *info, arg_VSUBADD2H_S12 *a)
{
    output("vsubadd2h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VSUBADD2H_CC(disassemble_info *info, arg_VSUBADD2H_CC *a)
{
    output("vsubadd2h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VSUBADD2H_CC_U6(disassemble_info *info, arg_VSUBADD2H_CC_U6 *a)
{
    output("vsubadd2h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VSUBADD4H(disassemble_info *info, arg_VSUBADD4H *a)
{
    output("vsubadd4h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VSUBADD4H_U6(disassemble_info *info, arg_VSUBADD4H_U6 *a)
{
    output("vsubadd4h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VSUBADD4H_S12(disassemble_info *info, arg_VSUBADD4H_S12 *a)
{
    output("vsubadd4h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VSUBADD4H_CC(disassemble_info *info, arg_VSUBADD4H_CC *a)
{
    output("vsubadd4h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VSUBADD4H_CC_U6(disassemble_info *info, arg_VSUBADD4H_CC_U6 *a)
{
    output("vsubadd4h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VMAC2H(disassemble_info *info, arg_VMAC2H *a)
{
    output("vmac2h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VMAC2H_U6(disassemble_info *info, arg_VMAC2H_U6 *a)
{
    output("vmac2h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VMAC2H_S12(disassemble_info *info, arg_VMAC2H_S12 *a)
{
    output("vmac2h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VMAC2H_CC(disassemble_info *info, arg_VMAC2H_CC *a)
{
    output("vmac2h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VMAC2H_CC_U6(disassemble_info *info, arg_VMAC2H_CC_U6 *a)
{
    output("vmac2h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VMAC2HU(disassemble_info *info, arg_VMAC2HU *a)
{
    output("vmac2hu", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VMAC2HU_U6(disassemble_info *info, arg_VMAC2HU_U6 *a)
{
    output("vmac2hu", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VMAC2HU_S12(disassemble_info *info, arg_VMAC2HU_S12 *a)
{
    output("vmac2hu", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VMAC2HU_CC(disassemble_info *info, arg_VMAC2HU_CC *a)
{
    output("vmac2hu.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VMAC2HU_CC_U6(disassemble_info *info, arg_VMAC2HU_CC_U6 *a)
{
    output("vmac2hu.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_VSUB4H(disassemble_info *info, arg_VSUB4H *a)
{
    output("vsub4h", "r%d, r%d, r%d", a->a, a->b, a->c);
    return true;
}

static bool trans_VSUB4H_U6(disassemble_info *info, arg_VSUB4H_U6 *a)
{
    output("vsub4h", "r%d, r%d, %d", a->a, a->b, a->u);
    return true;
}

static bool trans_VSUB4H_S12(disassemble_info *info, arg_VSUB4H_S12 *a)
{
    output("vsub4h", "r%d, r%d, %d", a->b, a->b, a->s);
    return true;
}

static bool trans_VSUB4H_CC(disassemble_info *info, arg_VSUB4H_CC *a)
{
    output("vsub4h.cc", "r%d, r%d", a->b, a->c);
    return true;
}

static bool trans_VSUB4H_CC_U6(disassemble_info *info, arg_VSUB4H_CC_U6 *a)
{
    output("vsub4h.cc", "r%d, %d", a->b, a->u);
    return true;
}

static bool trans_BL(disassemble_info *info, arg_BL *a)
{
    output("bl", "%d", a->s * 4);
    return true;
}

static bool trans_BL_D(disassemble_info *info, arg_BL_D *a)
{
    output("bl.d", "%d", a->s * 4);
    return true;
}

static bool trans_ST_S9(disassemble_info *info, arg_ST_S9 *a)
{
    output("st", "r%d, [r%d,%d]", a->c, a->b, a->s);
    return true;
}

static bool trans_LP(disassemble_info *info, arg_LP *a)
{
      output("lp", "%d", a->s);
      return true;
}

static bool trans_LPCC(disassemble_info *info, arg_LPCC *a)
{
      output("lpcc", "%d, %d", a->q, a->s);
      return true;
}

static bool trans_XBFU(disassemble_info *info, arg_XBFU *a)
{
      output("xbfu", "r%d, r%d, r%d", a->a, a->b, a->c);
      return true;
}

static bool trans_XBFU_U6(disassemble_info *info, arg_XBFU_U6 *a)
{
      output("xbfu", "r%d, r%d, %d", a->a, a->b, a->u);
      return true;
}

static bool trans_ST_S9_IMM(disassemble_info *info, arg_ST_S9_IMM *a)
{
    output("st", "%d, [r%d,%d]", a->c, a->b, a->s);
    return true;
}

static bool trans_LLOCK(disassemble_info *info, arg_LLOCK *a)
{
    output("llock", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_SCOND(disassemble_info *info, arg_SCOND *a)
{
    output("scond", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_EX(disassemble_info *info, arg_EX *a)
{
    output("ex", "r%d, [r%d]", a->b, a->c);
    return true;
}

static bool trans_DMB(disassemble_info *info, arg_DMB *a)
{
    output("dmb", "%d", a->c);
    return true;
}

static bool trans_REMU_U6(disassemble_info *info, arg_REMU_U6 *a)
{
    output("remu", "r%d, r%d, %d", a->a, a->b, a->u);
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