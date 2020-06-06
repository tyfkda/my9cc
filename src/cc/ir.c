#include "ir.h"

#include <assert.h>
#include <stdlib.h>  // malloc

#include "regalloc.h"
#include "table.h"
#include "util.h"
#include "x86_64.h"

#define SPILLED_REG_NO  (PHYSICAL_REG_MAX)

static VRegType vtVoidPtr = {.size = WORD_SIZE, .align = WORD_SIZE, .is_unsigned = false};
static VRegType vtBool    = {.size = 4, .align = 4, .is_unsigned = false};

int stackpos = 8;

static enum ConditionKind invert_cond(enum ConditionKind cond) {
  assert(COND_EQ <= cond && cond <= COND_UGT);
  if (cond <= COND_NE)
    return COND_NE + COND_EQ - cond;
  if (cond <= COND_ULT)
    return COND_LT + ((cond - COND_LT) ^ 2);
  return COND_ULT + ((cond - COND_ULT) ^ 2);
}

// Virtual register

VReg *new_vreg(int vreg_no, const VRegType *vtype, int flag) {
  VReg *vreg = malloc(sizeof(*vreg));
  vreg->v = vreg_no;
  vreg->r = -1;
  vreg->vtype = vtype;
  vreg->flag = flag;
  vreg->param_index = -1;
  vreg->offset = 0;
  return vreg;
}

// Register allocator

const char *kRegSizeTable[][7] = {
  { BL, R10B, R11B, R12B, R13B, R14B, R15B},
  { BX, R10W, R11W, R12W, R13W, R14W, R15W},
  {EBX, R10D, R11D, R12D, R13D, R14D, R15D},
  {RBX, R10,  R11,  R12,  R13,  R14,  R15},
};

#define kReg8s   (kRegSizeTable[0])
#define kReg32s  (kRegSizeTable[2])
#define kReg64s  (kRegSizeTable[3])

const char *kRegATable[] = {AL, AX, EAX, RAX};
const char *kRegDTable[] = {DL, DX, EDX, RDX};

#define CALLEE_SAVE_REG_COUNT  (5)
static struct {
  const char *reg;
  short bit;
} const kCalleeSaveRegs[] = {
  {RBX, 1 << 0},
  {R12, 1 << 3},
  {R13, 1 << 4},
  {R14, 1 << 5},
  {R15, 1 << 6},
};

//
RegAlloc *curra;

// Intermediate Representation

static IR *new_ir(enum IrKind kind) {
  IR *ir = malloc(sizeof(*ir));
  ir->kind = kind;
  ir->dst = ir->opr1 = ir->opr2 = NULL;
  if (curbb != NULL)
    vec_push(curbb->irs, ir);
  return ir;
}

static const int kPow2Table[] = {-1, 0, 1, -1, 2, -1, -1, -1, 3};
#define kPow2TableSize  ((int)(sizeof(kPow2Table) / sizeof(*kPow2Table)))

VReg *new_const_vreg(intptr_t value, const VRegType *vtype) {
  VReg *vreg = reg_alloc_spawn(curra, vtype, VRF_CONST);
  vreg->r = value;
  return vreg;
}

static intptr_t clamp_value(intptr_t value, const VRegType *vtype) {
  switch (vtype->size) {
  case 1:  value = (unsigned char)value; break;
  case 2:  value = (unsigned short)value; break;
  case 4:  value = (unsigned int)value; break;
  default:  break;
  }
  return value;
}

VReg *new_ir_bop(enum IrKind kind, VReg *opr1, VReg *opr2, const VRegType *vtype) {
  if (opr1->flag & VRF_CONST) {
    if (opr2->flag & VRF_CONST) {
      intptr_t value = 0;
      switch (kind) {
      case IR_ADD:     value = opr1->r + opr2->r; break;
      case IR_SUB:     value = opr1->r - opr2->r; break;
      case IR_MUL:     value = opr1->r * opr2->r; break;

      case IR_DIV:
      case IR_DIVU:
      case IR_MOD:
      case IR_MODU:
        if (opr2->r == 0)
          error("Divide by 0");
        switch (kind) {
        case IR_DIV:  value = opr1->r / opr2->r; break;
        case IR_DIVU: value = (uintptr_t)opr1->r / opr2->r; break;
        case IR_MOD:  value = opr1->r / opr2->r; break;
        case IR_MODU: value = (uintptr_t)opr1->r / opr2->r; break;
        default: assert(false); break;
        }
        break;

      case IR_BITAND:  value = opr1->r & opr2->r; break;
      case IR_BITOR:   value = opr1->r | opr2->r; break;
      case IR_BITXOR:  value = opr1->r ^ opr2->r; break;
      case IR_LSHIFT:  value = opr1->r << opr2->r; break;
      case IR_RSHIFT:
        //assert(opr1->type->kind == TY_NUM);
        if (opr1->vtype->is_unsigned)
          value = (uintptr_t)opr1->r >> opr2->r;
        else
          value = opr1->r >> opr2->r;
        break;
      default: assert(false); break;
      }
      return new_const_vreg(clamp_value(value, vtype), vtype);
    } else {
      switch (kind) {
      case IR_ADD:
      case IR_SUB:
        if (opr1->r == 0)
          return opr2;
        break;
      case IR_MUL:
        if (opr1->r == 1)
          return opr2;
        break;
      case IR_DIV:
      case IR_DIVU:
      case IR_MOD:
      case IR_MODU:
        if (opr1->r == 0)
          return opr1;  // TODO: whether opr2 is zero.
        break;
      case IR_BITAND:
        if (opr1->r == 0)
          return opr1;
        break;
      case IR_BITOR:
        if (opr1->r == 0)
          return opr2;
        break;
      case IR_BITXOR:
        if (opr1->r == 0)
          return opr2;
        break;
      case IR_LSHIFT:
      case IR_RSHIFT:
        if (opr1->r == 0)
          return opr1;
        break;
      default:
        break;
      }
    }
  } else {
    if (opr2->flag & VRF_CONST) {
      switch (kind) {
      case IR_ADD:
      case IR_SUB:
        if (opr2->r == 0)
          return opr1;
        break;
      case IR_MUL:
      case IR_DIV:
      case IR_DIVU:
        if (opr2->r == 0)
          error("Divide by 0");
        if (opr2->r == 1)
          return opr1;
        break;
      case IR_BITAND:
        if (opr2->r == 0)
          return opr2;
        break;
      case IR_BITOR:
        if (opr2->r == 0)
          return opr1;
        break;
      case IR_BITXOR:
        if (opr2->r == 0)
          return opr1;
        break;
      case IR_LSHIFT:
      case IR_RSHIFT:
        if (opr2->r == 0)
          return opr1;
        break;
      default:
        break;
      }
    }
  }

  IR *ir = new_ir(kind);
  ir->opr1 = opr1;
  ir->opr2 = opr2;
  return ir->dst = reg_alloc_spawn(curra, vtype, 0);
}

VReg *new_ir_unary(enum IrKind kind, VReg *opr, const VRegType *vtype) {
  if (opr->flag & VRF_CONST) {
    intptr_t value = 0;
    switch (kind) {
    case IR_NEG:     value = -opr->r; break;
    case IR_NOT:     value = !opr->r; break;
    case IR_BITNOT:  value = ~opr->r; break;
    default: assert(false); break;
    }
    return new_const_vreg(clamp_value(value, vtype), vtype);
  }

  IR *ir = new_ir(kind);
  ir->opr1 = opr;
  return ir->dst = reg_alloc_spawn(curra, vtype, 0);
}

VReg *new_ir_ptradd(int offset, VReg *base, VReg *index, int scale, const VRegType *vtype) {
  IR *ir = new_ir(IR_PTRADD);
  ir->opr1 = base;
  ir->opr2 = index;
  ir->ptradd.offset = offset;
  ir->ptradd.scale = scale;
  return ir->dst = reg_alloc_spawn(curra, vtype, 0);
}

VReg *new_ir_bofs(VReg *src) {
  IR *ir = new_ir(IR_BOFS);
  ir->opr1 = src;
  return ir->dst = reg_alloc_spawn(curra, &vtVoidPtr, 0);
}

VReg *new_ir_iofs(const Name *label, bool global) {
  IR *ir = new_ir(IR_IOFS);
  ir->iofs.label = label;
  ir->iofs.global = global;
  return ir->dst = reg_alloc_spawn(curra, &vtVoidPtr, 0);
}

VReg *new_ir_sofs(VReg *src) {
  IR *ir = new_ir(IR_SOFS);
  ir->opr1 = src;
  return ir->dst = reg_alloc_spawn(curra, &vtVoidPtr, 0);
}

void new_ir_store(VReg *dst, VReg *src) {
  IR *ir = new_ir(IR_STORE);
  ir->opr1 = src;
  ir->opr2 = dst;  // `dst` is used by indirect, so it is not actually `dst`.
}

void new_ir_cmp(VReg *opr1, VReg *opr2) {
  IR *ir = new_ir(IR_CMP);
  ir->opr1 = opr1;
  ir->opr2 = opr2;
}

void new_ir_test(VReg *reg) {
  IR *ir = new_ir(IR_TEST);
  ir->opr1 = reg;
}

void new_ir_incdec(enum IrKind kind, VReg *reg, int size, intptr_t value) {
  IR *ir = new_ir(kind);
  ir->opr1 = reg;
  ir->value = value;
  ir->incdec.size = size;
}

VReg *new_ir_cond(enum ConditionKind cond) {
  IR *ir = new_ir(IR_COND);
  ir->cond.kind = cond;
  return ir->dst = reg_alloc_spawn(curra, &vtBool, 0);
}

void new_ir_jmp(enum ConditionKind cond, BB *bb) {
  IR *ir = new_ir(IR_JMP);
  ir->jmp.bb = bb;
  ir->jmp.cond = cond;
}

void new_ir_pusharg(VReg *vreg) {
  IR *ir = new_ir(IR_PUSHARG);
  ir->opr1 = vreg;
}

IR *new_ir_precall(int arg_count, int stack_args_size) {
  IR *ir = new_ir(IR_PRECALL);
  ir->precall.arg_count = arg_count;
  ir->precall.stack_args_size = stack_args_size;
  ir->precall.stack_aligned = false;
  return ir;
}

VReg *new_ir_call(const Name *label, bool global, VReg *freg, int arg_count, const VRegType *result_type, IR *precall) {
  IR *ir = new_ir(IR_CALL);
  ir->call.label = label;
  ir->call.global = global;
  ir->opr1 = freg;
  ir->call.precall = precall;
  ir->call.arg_count = arg_count;
  return ir->dst = reg_alloc_spawn(curra, result_type, 0);
}

void new_ir_result(VReg *reg) {
  IR *ir = new_ir(IR_RESULT);
  ir->opr1 = reg;
}

void new_ir_addsp(int value) {
  IR *ir = new_ir(IR_ADDSP);
  ir->value = value;
}

VReg *new_ir_cast(VReg *vreg, const VRegType *dsttype, int srcsize, bool is_unsigned) {
  IR *ir = new_ir(IR_CAST);
  ir->opr1 = vreg;
  ir->cast.srcsize = srcsize;
  ir->cast.is_unsigned = is_unsigned;
  return ir->dst = reg_alloc_spawn(curra, dsttype, 0);
}

void new_ir_mov(VReg *dst, VReg *src) {
  IR *ir = new_ir(IR_MOV);
  ir->dst = dst;
  ir->opr1 = src;
}

void new_ir_memcpy(VReg *dst, VReg *src, size_t size) {
  IR *ir = new_ir(IR_MEMCPY);
  ir->opr1 = src;
  ir->opr2 = dst;
  ir->memcpy.size = size;
}

void new_ir_clear(VReg *reg, size_t size) {
  IR *ir = new_ir(IR_CLEAR);
  ir->opr1 = reg;
  ir->clear.size = size;
}

void new_ir_asm(const char *asm_) {
  IR *ir = new_ir(IR_ASM);
  ir->asm_.str = asm_;
}

IR *new_ir_load_spilled(VReg *reg, int offset, int size) {
  IR *ir = new_ir(IR_LOAD_SPILLED);
  ir->value = offset;
  ir->dst = reg;
  ir->load_spilled.size = size;
  return ir;
}

IR *new_ir_store_spilled(VReg *reg, int offset) {
  IR *ir = new_ir(IR_STORE_SPILLED);
  ir->value = offset;
  ir->opr1 = reg;
  return ir;
}

static void ir_memcpy(int dst_reg, int src_reg, ssize_t size) {
  const char *dst = kReg64s[dst_reg];
  const char *src = kReg64s[src_reg];

  // Break %rcx, %dl
  switch (size) {
  case 1:
    MOV(INDIRECT(src, NULL, 1), DL);
    MOV(DL, INDIRECT(dst, NULL, 1));
    break;
  case 2:
    MOV(INDIRECT(src, NULL, 1), DX);
    MOV(DX, INDIRECT(dst, NULL, 1));
    break;
  case 4:
    MOV(INDIRECT(src, NULL, 1), EDX);
    MOV(EDX, INDIRECT(dst, NULL, 1));
    break;
  case 8:
    MOV(INDIRECT(src, NULL, 1), RDX);
    MOV(RDX, INDIRECT(dst, NULL, 1));
    break;
  default:
    {
      const Name *name = alloc_label();
      const char *label = fmt_name(name);
      PUSH(src);
      MOV(IM(size), RCX);
      EMIT_LABEL(label);
      MOV(INDIRECT(src, NULL, 1), DL);
      MOV(DL, INDIRECT(dst, NULL, 1));
      INC(src);
      INC(dst);
      DEC(RCX);
      JNE(label);
      POP(src);
    }
    break;
  }
}

static void ir_out(IR *ir) {
  switch (ir->kind) {
  case IR_BOFS:
    assert(!(ir->opr1->flag & VRF_CONST));
    LEA(OFFSET_INDIRECT(ir->opr1->offset, RBP, NULL, 1), kReg64s[ir->dst->r]);
    break;

  case IR_IOFS:
    {
      const char *label = fmt_name(ir->iofs.label);
      if (ir->iofs.global)
        label = MANGLE(label);
      LEA(LABEL_INDIRECT(label, RIP), kReg64s[ir->dst->r]);
    }
    break;

  case IR_SOFS:
    assert(ir->opr1->flag & VRF_CONST);
    LEA(OFFSET_INDIRECT(ir->opr1->r, RSP, NULL, 1), kReg64s[ir->dst->r]);
    break;

  case IR_LOAD:
    {
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      MOV(INDIRECT(kReg64s[ir->opr1->r], NULL, 1), regs[ir->dst->r]);
    }
    break;

  case IR_STORE:
    {
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(!(ir->opr2->flag & VRF_CONST));
      assert(0 <= ir->opr1->vtype->size && ir->opr1->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      MOV(regs[ir->opr1->r], INDIRECT(kReg64s[ir->opr2->r], NULL, 1));
    }
    break;

  case IR_ADD:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr2->flag & VRF_CONST)
        ADD(im(ir->opr2->r), regs[ir->dst->r]);
      else
        ADD(regs[ir->opr2->r], regs[ir->dst->r]);
    }
    break;

  case IR_SUB:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr2->flag & VRF_CONST)
        SUB(im(ir->opr2->r), regs[ir->dst->r]);
      else
        SUB(regs[ir->opr2->r], regs[ir->dst->r]);
    }
    break;

  case IR_PTRADD:
    {
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      VReg *base = ir->opr1;
      VReg *index = ir->opr2;
      if (index == NULL && ir->ptradd.scale == 1 && ir->ptradd.offset == 0) {
        if (ir->dst->r == base->r)
          ;  // No need to move.
        else
          MOV(regs[base->r], regs[ir->dst->r]);
      } else if (ir->dst->r == base->r && ir->ptradd.scale == 1 && ir->ptradd.offset == 0) {
        ADD(regs[index->r], regs[ir->dst->r]);
      } else if (index != NULL && ir->dst->r == index->r && ir->ptradd.scale == 1 && ir->ptradd.offset == 0) {
        ADD(regs[base->r], regs[ir->dst->r]);
      } else {
        LEA(OFFSET_INDIRECT(ir->ptradd.offset, regs[base->r], index != NULL ? regs[index->r] : NULL, ir->ptradd.scale), regs[ir->dst->r]);
      }
    }
    break;

  case IR_MUL:
    {
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      assert(!(ir->opr1->flag & VRF_CONST));
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      const char *a = kRegATable[pow];
      MOV(regs[ir->opr1->r], a);
      const char *opr2;
      if (ir->opr2->flag & VRF_CONST) {
        MOV(im(ir->opr2->r), regs[SPILLED_REG_NO]);
        opr2 = regs[SPILLED_REG_NO];
      } else {
        opr2 = regs[ir->opr2->r];
      }
      MUL(opr2);
      MOV(a, regs[ir->dst->r]);
    }
    break;

  case IR_DIV:
  case IR_DIVU:
    assert(!(ir->opr1->flag & VRF_CONST));
    if (ir->dst->vtype->size == 1) {
      if (ir->kind == IR_DIV) {
        MOVSX(kReg8s[ir->opr1->r], AX);
        const char *opr2;
        if (ir->opr2->flag & VRF_CONST) {
          opr2 = kReg8s[SPILLED_REG_NO];
          MOV(im(ir->opr2->r), opr2);
        } else {
          opr2 = kReg8s[ir->opr2->r];
        }
        IDIV(opr2);
      } else {
        MOVZX(kReg8s[ir->opr1->r], AX);
        const char *opr2;
        if (ir->opr2->flag & VRF_CONST) {
          opr2 = kReg8s[SPILLED_REG_NO];
          MOV(im(ir->opr2->r), opr2);
        } else {
          opr2 = kReg8s[ir->opr2->r];
        }
        DIV(opr2);
      }
      MOV(AL, kReg8s[ir->dst->r]);
    } else {
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      const char *a = kRegATable[pow];
      MOV(regs[ir->opr1->r], a);
      const char *opr2;
      if (ir->opr2->flag & VRF_CONST) {
        opr2 = regs[SPILLED_REG_NO];
        MOV(im(ir->opr2->r), opr2);
      } else {
        opr2 = regs[ir->opr2->r];
      }
      if (ir->kind == IR_DIV) {
        switch (pow) {
        case 1:  CWTL(); break;
        case 2:  CLTD(); break;
        case 3:  CQTO(); break;
        default: assert(false); break;
        }
        IDIV(opr2);
      } else {
        switch (pow) {
        case 1:  XOR(DX, DX); break;
        case 2:  XOR(EDX, EDX); break;
        case 3:  XOR(EDX, EDX); break;  // Clear 64bit register.
        default: assert(false); break;
        }
        DIV(opr2);
      }
      MOV(a, regs[ir->dst->r]);
    }
    break;

  case IR_MOD:
  case IR_MODU:
    assert(!(ir->opr1->flag & VRF_CONST));
    if (ir->opr1->vtype->size == 1) {
      if (ir->kind == IR_MOD) {
        MOVSX(kReg8s[ir->opr1->r], AX);
        const char *opr2;
        if (ir->opr2->flag & VRF_CONST) {
          opr2 = kReg8s[SPILLED_REG_NO];
          MOV(im(ir->opr2->r), opr2);
        } else {
          opr2 = kReg8s[ir->opr2->r];
        }
        IDIV(opr2);
      } else {
        MOVZX(kReg8s[ir->opr1->r], AX);
        const char *opr2;
        if (ir->opr2->flag & VRF_CONST) {
          opr2 = kReg8s[SPILLED_REG_NO];
          MOV(im(ir->opr2->r), opr2);
        } else {
          opr2 = kReg8s[ir->opr2->r];
        }
        DIV(opr2);
      }
      MOV(AH, kReg8s[ir->dst->r]);
    } else {
      assert(0 <= ir->opr1->vtype->size && ir->opr1->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      const char *a = kRegATable[pow];
      const char *d = kRegDTable[pow];
      MOV(regs[ir->opr1->r], a);
      const char *opr2;
      if (ir->opr2->flag & VRF_CONST) {
        opr2 = regs[SPILLED_REG_NO];
        MOV(im(ir->opr2->r), opr2);
      } else {
        opr2 = regs[ir->opr2->r];
      }
      if (ir->kind == IR_MOD) {
        switch (pow) {
        case 1:  CWTL(); break;
        case 2:  CLTD(); break;
        case 3:  CQTO(); break;
        default: assert(false); break;
        }
        IDIV(opr2);
      } else {
        switch (pow) {
        case 1:  XOR(DX, DX); break;
        case 2:  XOR(EDX, EDX); break;
        case 3:  XOR(EDX, EDX); break;  // Clear 64bit register.
        default: assert(false); break;
        }
        DIV(opr2);
      }
      MOV(d, regs[ir->dst->r]);
    }
    break;

  case IR_BITAND:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr2->flag & VRF_CONST)
        AND(im(ir->opr2->r), regs[ir->dst->r]);
      else
        AND(regs[ir->opr2->r], regs[ir->dst->r]);
    }
    break;

  case IR_BITOR:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr2->flag & VRF_CONST)
        OR(im(ir->opr2->r), regs[ir->dst->r]);
      else
        OR(regs[ir->opr2->r], regs[ir->dst->r]);
    }
    break;

  case IR_BITXOR:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr2->flag & VRF_CONST)
        XOR(im(ir->opr2->r), regs[ir->dst->r]);
      else
        XOR(regs[ir->opr2->r], regs[ir->dst->r]);
    }
    break;

  case IR_LSHIFT:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr2->flag & VRF_CONST) {
        SHL(im(ir->opr2->r), regs[ir->dst->r]);
      } else {
        MOV(kReg8s[ir->opr2->r], CL);
        SHL(CL, regs[ir->dst->r]);
      }
    }
    break;
  case IR_RSHIFT:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->opr1->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr2->flag & VRF_CONST) {
        SHR(im(ir->opr2->r), regs[ir->dst->r]);
      } else {
        MOV(kReg8s[ir->opr2->r], CL);
        SHR(CL, regs[ir->dst->r]);
      }
    }
    break;

  case IR_CMP:
    {
      assert(0 <= ir->opr1->vtype->size && ir->opr1->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      const char *opr1;
      if (ir->opr1->flag & VRF_CONST) {
        opr1 = kRegATable[pow];
        MOV(im(ir->opr1->r), opr1);
      } else {
        opr1 = regs[ir->opr1->r];
      }
      const char *opr2 = (ir->opr2->flag & VRF_CONST) ? im(ir->opr2->r) : regs[ir->opr2->r];
      CMP(opr2, opr1);
    }
    break;

  case IR_INC:
    {
      assert(!(ir->opr1->flag & VRF_CONST));
      const char *reg = INDIRECT(kReg64s[ir->opr1->r], NULL, 1);
      if (ir->value == 1) {
        switch (ir->incdec.size) {
        case 1:  INCB(reg); break;
        case 2:  INCW(reg); break;
        case 4:  INCL(reg); break;
        case 8:  INCQ(reg); break;
        default:  assert(false); break;
        }
      } else {
        assert(ir->incdec.size == 8);
        intptr_t value = ir->value;
        if (value <= ((1L << 31) - 1)) {
          ADDQ(IM(value), reg);
        } else {
          MOV(IM(value), RAX);
          ADD(RAX, reg);
        }
      }
    }
    break;

  case IR_DEC:
    {
      assert(!(ir->opr1->flag & VRF_CONST));
      const char *reg = INDIRECT(kReg64s[ir->opr1->r], NULL, 1);
      if (ir->value == 1) {
        switch (ir->incdec.size) {
        case 1:  DECB(reg); break;
        case 2:  DECW(reg); break;
        case 4:  DECL(reg); break;
        case 8:  DECQ(reg); break;
        default:  assert(false); break;
        }
      } else {
        assert(ir->incdec.size == 8);
        intptr_t value = ir->value;
        if (value <= ((1L << 31) - 1)) {
          SUBQ(IM(value), reg);
        } else {
          MOV(IM(value), RAX);
          SUB(RAX, reg);
        }
      }
    }
    break;

  case IR_NEG:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->dst->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      NEG(regs[ir->dst->r]);
    }
    break;

  case IR_NOT:
    {
      assert(0 <= ir->opr1->vtype->size && ir->opr1->vtype->size < kPow2TableSize);
      assert(!(ir->opr1->flag & VRF_CONST));
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      const char *opr1 = regs[ir->opr1->r];
      TEST(opr1, opr1);
      const char *dst8 = kReg8s[ir->dst->r];
      SETE(dst8);
      MOVSX(dst8, kReg32s[ir->dst->r]);
    }
    break;

  case IR_BITNOT:
    {
      assert(ir->dst->r == ir->opr1->r);
      assert(!(ir->dst->flag & VRF_CONST));
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      NOT(regs[ir->dst->r]);
    }
    break;

  case IR_COND:
    {
      assert(!(ir->dst->flag & VRF_CONST));
      const char *dst = kReg8s[ir->dst->r];
      switch (ir->cond.kind) {
      case COND_EQ:  SETE(dst); break;
      case COND_NE:  SETNE(dst); break;
      case COND_LT:  SETL(dst); break;
      case COND_GT:  SETG(dst); break;
      case COND_LE:  SETLE(dst); break;
      case COND_GE:  SETGE(dst); break;
      case COND_ULT: SETB(dst); break;
      case COND_UGT: SETA(dst); break;
      case COND_ULE: SETBE(dst); break;
      case COND_UGE: SETAE(dst); break;
      default: assert(false); break;
      }
      MOVSX(dst, kReg32s[ir->dst->r]);  // Assume bool is 4 byte.
    }
    break;

  case IR_TEST:
    {
      assert(0 <= ir->opr1->vtype->size && ir->opr1->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      const char *opr1;
      if (ir->opr1->flag & VRF_CONST) {
        opr1 = kRegATable[pow];
        MOV(im(ir->opr1->r), opr1);
      } else {
        opr1 = regs[ir->opr1->r];
      }
      TEST(opr1, opr1);
    }
    break;

  case IR_JMP:
    switch (ir->jmp.cond) {
    case COND_ANY:  JMP(fmt_name(ir->jmp.bb->label)); break;
    case COND_EQ:   JE(fmt_name(ir->jmp.bb->label)); break;
    case COND_NE:   JNE(fmt_name(ir->jmp.bb->label)); break;
    case COND_LT:   JL(fmt_name(ir->jmp.bb->label)); break;
    case COND_GT:   JG(fmt_name(ir->jmp.bb->label)); break;
    case COND_LE:   JLE(fmt_name(ir->jmp.bb->label)); break;
    case COND_GE:   JGE(fmt_name(ir->jmp.bb->label)); break;
    case COND_ULT:  JB(fmt_name(ir->jmp.bb->label)); break;
    case COND_UGT:  JA(fmt_name(ir->jmp.bb->label)); break;
    case COND_ULE:  JBE(fmt_name(ir->jmp.bb->label)); break;
    case COND_UGE:  JAE(fmt_name(ir->jmp.bb->label)); break;
    default:  assert(false); break;
    }
    break;

  case IR_PRECALL:
    {
      // Caller save.
      PUSH(R10); PUSH_STACK_POS();
      PUSH(R11); PUSH_STACK_POS();

      int align_stack = (stackpos + ir->precall.stack_args_size) & 15;
      if (align_stack != 0) {
        align_stack = 16 - align_stack;
        SUB(IM(align_stack), RSP);
        stackpos += align_stack;
      }
      ir->precall.stack_aligned = align_stack;
    }
    break;

  case IR_PUSHARG:
    if (ir->opr1->flag & VRF_CONST) {
      PUSH(im(ir->opr1->r)); PUSH_STACK_POS();
    } else {
      PUSH(kReg64s[ir->opr1->r]); PUSH_STACK_POS();
    }
    break;

  case IR_CALL:
    {
      static const char *kArgReg64s[] = {RDI, RSI, RDX, RCX, R8, R9};

      // Pop register arguments.
      int reg_args = MIN(ir->call.arg_count, MAX_REG_ARGS);
      for (int i = 0; i < reg_args; ++i) {
        POP(kArgReg64s[i]); POP_STACK_POS();
      }

      if (ir->call.label != NULL) {
        const char *label = fmt_name(ir->call.label);
        if (ir->call.global)
          CALL(MANGLE(label));
        else
          CALL(label);
      } else {
        assert(!(ir->opr1->flag & VRF_CONST));
        CALL(fmt("*%s", kReg64s[ir->opr1->r]));
      }

      int align_stack = ir->call.precall->precall.stack_aligned + ir->call.precall->precall.stack_args_size;
      if (align_stack != 0) {
        ADD(IM(align_stack), RSP);
        stackpos -= align_stack;
      }

      // Resore caller save registers.
      POP(R11); POP_STACK_POS();
      POP(R10); POP_STACK_POS();

      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      MOV(kRegATable[pow], regs[ir->dst->r]);
    }
    break;

  case IR_RESULT:
    {
      assert(0 <= ir->opr1->vtype->size && ir->opr1->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr1->flag & VRF_CONST)
        MOV(im(ir->opr1->r), kRegATable[pow]);
      else
        MOV(regs[ir->opr1->r], kRegATable[pow]);
    }
    break;

  case IR_ADDSP:
    if (ir->value > 0)
      ADD(IM(ir->value), RSP);
    else
      SUB(IM(-ir->value), RSP);
    stackpos -= ir->value;
    break;

  case IR_CAST:
    if (ir->opr1->flag & VRF_CONST) {
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      int powd = kPow2Table[ir->dst->vtype->size];
      MOV(im(ir->opr1->r), kRegSizeTable[powd][ir->dst->r]);
    } else {
      if (ir->dst->vtype->size <= ir->cast.srcsize) {
        if (ir->dst->r != ir->opr1->r) {
          assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
          int pow = kPow2Table[ir->dst->vtype->size];
          assert(0 <= pow && pow < 4);
          const char **regs = kRegSizeTable[pow];
          MOV(regs[ir->opr1->r], regs[ir->dst->r]);
        }
      } else {
        assert(0 <= ir->cast.srcsize && ir->cast.srcsize < kPow2TableSize);
        int pows = kPow2Table[ir->cast.srcsize];
        assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
        int powd = kPow2Table[ir->dst->vtype->size];
        assert(0 <= pows && pows < 4);
        assert(0 <= powd && powd < 4);
        if (ir->cast.is_unsigned) {
          if (pows == 2 && powd == 3) {
            // MOVZX %64bit, %32bit doesn't exist!
            MOV(kRegSizeTable[pows][ir->opr1->r], kRegSizeTable[pows][ir->dst->r]);
          } else {
            MOVZX(kRegSizeTable[pows][ir->opr1->r], kRegSizeTable[powd][ir->dst->r]);
          }
        } else {
          MOVSX(kRegSizeTable[pows][ir->opr1->r], kRegSizeTable[powd][ir->dst->r]);
        }
      }
    }
    break;

  case IR_MOV:
    {
      assert(0 <= ir->dst->vtype->size && ir->dst->vtype->size < kPow2TableSize);
      assert(!(ir->dst->flag & VRF_CONST));
      int pow = kPow2Table[ir->dst->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      if (ir->opr1->flag & VRF_CONST) {
        MOV(im(ir->opr1->r), regs[ir->dst->r]);
      } else {
        if (ir->opr1->r != ir->dst->r)
          MOV(regs[ir->opr1->r], regs[ir->dst->r]);
      }
    }
    break;

  case IR_MEMCPY:
    assert(!(ir->opr1->flag & VRF_CONST));
    assert(!(ir->opr2->flag & VRF_CONST));
    ir_memcpy(ir->opr2->r, ir->opr1->r, ir->memcpy.size);
    break;

  case IR_CLEAR:
    {
      assert(!(ir->opr1->flag & VRF_CONST));
      const char *loop = fmt_name(alloc_label());
      MOV(kReg64s[ir->opr1->r], RSI);
      MOV(IM(ir->clear.size), EDI);
      XOR(AL, AL);
      EMIT_LABEL(loop);
      MOV(AL, INDIRECT(RSI, NULL, 1));
      INC(RSI);
      DEC(EDI);
      JNE(loop);
    }
    break;

  case IR_ASM:
    EMIT_ASM0(ir->asm_.str);
    break;

  case IR_LOAD_SPILLED:
    {
      assert(0 <= ir->load_spilled.size && ir->load_spilled.size < kPow2TableSize);
      int pow = kPow2Table[ir->load_spilled.size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      MOV(OFFSET_INDIRECT(ir->value, RBP, NULL, 1), regs[SPILLED_REG_NO]);
    }
    break;

  case IR_STORE_SPILLED:
    {
      assert(0 <= ir->opr1->vtype->size && ir->opr1->vtype->size < kPow2TableSize);
      int pow = kPow2Table[ir->opr1->vtype->size];
      assert(0 <= pow && pow < 4);
      const char **regs = kRegSizeTable[pow];
      MOV(regs[SPILLED_REG_NO], OFFSET_INDIRECT(ir->value, RBP, NULL, 1));
    }
    break;

  default:
    assert(false);
    break;
  }
}

// Basic Block

BB *curbb;

BB *new_bb(void) {
  BB *bb = malloc(sizeof(*bb));
  bb->next = NULL;
  bb->label = alloc_label();
  bb->irs = new_vector();
  bb->in_regs = NULL;
  bb->out_regs = NULL;
  bb->assigned_regs = NULL;
  return bb;
}

BB *bb_split(BB *bb) {
  BB *cc = new_bb();
  cc->next = bb->next;
  bb->next = cc;
  return cc;
}

void bb_insert(BB *bb, BB *cc) {
  cc->next = bb->next;
  bb->next = cc;
}

//

BBContainer *new_func_blocks(void) {
  BBContainer *bbcon = malloc(sizeof(*bbcon));
  bbcon->bbs = new_vector();
  return bbcon;
}

static IR *is_last_jmp(BB *bb) {
  int len;
  IR *ir;
  if ((len = bb->irs->len) > 0 &&
      (ir = bb->irs->data[len - 1])->kind == IR_JMP)
    return ir;
  return NULL;
}

static IR *is_last_any_jmp(BB *bb) {
  IR *ir = is_last_jmp(bb);
  return ir != NULL && ir->jmp.cond == COND_ANY ? ir : NULL;
}

static void replace_jmp_destination(BBContainer *bbcon, BB *src, BB *dst) {
  Vector *bbs = bbcon->bbs;
  for (int j = 0; j < bbs->len; ++j) {
    BB *bb = bbs->data[j];
    if (bb == src)
      continue;

    IR *ir = is_last_jmp(bb);
    if (ir != NULL && ir->jmp.bb == src)
      ir->jmp.bb = dst;
  }
}

void remove_unnecessary_bb(BBContainer *bbcon) {
  Vector *bbs = bbcon->bbs;
  for (;;) {
    bool again = false;
    for (int i = 0; i < bbs->len - 1; ++i) {  // Make last one keeps alive.
      BB *bb = bbs->data[i];
      IR *ir;
      if (bb->irs->len == 0) {  // Empty BB.
        replace_jmp_destination(bbcon, bb, bb->next);
      } else if (bb->irs->len == 1 && (ir = is_last_any_jmp(bb)) != NULL) {  // jmp only.
        replace_jmp_destination(bbcon, bb, ir->jmp.bb);
        if (i == 0)
          continue;
        BB *pbb = bbs->data[i - 1];
        if (!is_last_jmp(pbb))
          continue;
        if (!is_last_any_jmp(pbb)) {  // Fallthrough pass exists.
          IR *ir0 = pbb->irs->data[pbb->irs->len - 1];
          if (ir0->jmp.bb != bb->next)  // Non skip jmp: Keep bb connection.
            continue;
          // Invert prev jmp condition and change jmp destination.
          ir0->jmp.cond = invert_cond(ir0->jmp.cond);
          ir0->jmp.bb = ir->jmp.bb;
        }
      } else {
        continue;
      }

      if (i > 0) {
        BB *pbb = bbs->data[i - 1];
        pbb->next = bb->next;
      }

      vec_remove_at(bbs, i);
      --i;
      again = true;
    }
    if (!again)
      break;
  }

  // Remove jmp to next instruction.
  for (int i = 0; i < bbs->len - 1; ++i) {  // Make last one keeps alive.
    BB *bb = bbs->data[i];
    IR *ir = is_last_any_jmp(bb);
    if (ir != NULL && ir->jmp.bb == bb->next)
      vec_pop(bb->irs);
  }
}

void push_callee_save_regs(short used) {
  for (int i = 0; i < CALLEE_SAVE_REG_COUNT; ++i) {
    if (used & kCalleeSaveRegs[i].bit) {
      PUSH(kCalleeSaveRegs[i].reg); PUSH_STACK_POS();
    }
  }
}

void pop_callee_save_regs(short used) {
  for (int i = CALLEE_SAVE_REG_COUNT; --i >= 0;) {
    if (used & kCalleeSaveRegs[i].bit) {
      POP(kCalleeSaveRegs[i].reg); POP_STACK_POS();
    }
  }
}

void emit_bb_irs(BBContainer *bbcon) {
  for (int i = 0; i < bbcon->bbs->len; ++i) {
    BB *bb = bbcon->bbs->data[i];
#ifndef NDEBUG
    // Check BB connection.
    if (i < bbcon->bbs->len - 1) {
      BB *nbb = bbcon->bbs->data[i + 1];
      UNUSED(nbb);
      assert(bb->next == nbb);
    } else {
      assert(bb->next == NULL);
    }
#endif

    EMIT_LABEL(fmt_name(bb->label));
    for (int j = 0; j < bb->irs->len; ++j) {
      IR *ir = bb->irs->data[j];
      ir_out(ir);
    }
  }
}
