#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "exec/translator.h"
#include "qemu/log.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef HELPER_H

TCGv cpu_gpr[32];
TCGv cpu_pc;

void arc_translate_init(void) 
{
	int i;
	static const char * const reg_names[] = 
    {
        "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "gp",  "fp",  "sp",  "ilink", "blink", "lp_count"
    	};

	for (i = 0; i < 32; i++) 
    {
        cpu_gpr[i] = tcg_global_mem_new(tcg_env,
                                        offsetof(CPUArchState, r[i]),
                                        reg_names[i]);
    }

    cpu_pc = tcg_global_mem_new(tcg_env,
                                offsetof(CPUArchState, pc),
                                "pc");
}

typedef struct DisasContext
{
    DisasContextBase base;
    CPUArchState *env;
} DisasContext;

static void gen_exception(DisasContext *ctx, int excp)
{
    TCGv_i32 t = tcg_constant_i32(excp);
    gen_helper_raise_exception(tcg_env, t);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_illegal(DisasContext *ctx)
{
    gen_exception(ctx, EXCP_ILLEGAL);
}

#include "decode-insn.c.inc"


static bool trans_MOV(DisasContext *ctx, arg_mov *a)
{
    if (a->b >= 32)
        return false;

    if (a->c == 62) {
        uint32_t limm = translator_ldl_end(ctx->env, &ctx->base,
                                           ctx->base.pc_next + 4, MO_LE);
        tcg_gen_movi_tl(cpu_gpr[a->b], limm);
        ctx->base.pc_next += 4;
    } else {
        if (a->c >= 32)
            return false;
        tcg_gen_mov_tl(cpu_gpr[a->b], cpu_gpr[a->c]);
    }

    return true;
}

static void arc_tr_init_disas_context(DisasContextBase *dcb, CPUState *cs)
{

}

static void arc_tr_tb_start(DisasContextBase *db, CPUState *cs)
{

}

static void arc_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    tcg_gen_insn_start(dcbase->pc_next,0,0);
}

static void arc_tr_translate_insn(DisasContextBase *dcbase,CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUArchState *env = cpu_env(cs);
    ctx->env = env;
    uint32_t insn = translator_ldl_end(env, &ctx->base, ctx->base.pc_next, MO_LE);
    insn = (insn << 16)|(insn >> 16);
    if ((insn & 0xFFC00000) == 0x78000000) 
    {
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 2);
        gen_exception(ctx, EXCP_SYSCALL);
        ctx->base.pc_next += 2;
        return;
    }

    

    if (!decode(ctx, insn)) 
    {
        gen_exception_illegal(ctx);
    }
    ctx->base.pc_next += 4;
}

static void arc_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase,DisasContext,base);

    switch (ctx->base.is_jmp) 
    {
    case DISAS_TOO_MANY:
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_NORETURN:
        break;
    default:
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
        tcg_gen_exit_tb(NULL, 0);
        break;
    }
}
static const TranslatorOps arc_tr_ops = {.init_disas_context = arc_tr_init_disas_context, .tb_start = arc_tr_tb_start, .insn_start = arc_tr_insn_start, .translate_insn = arc_tr_translate_insn, .tb_stop = arc_tr_tb_stop,};

void arc_translate_code(CPUState *cs, TranslationBlock *tb, int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext ctx;
    translator_loop(cs, tb, max_insns, pc, host_pc, &arc_tr_ops, &ctx.base);
}

