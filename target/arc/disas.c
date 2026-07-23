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