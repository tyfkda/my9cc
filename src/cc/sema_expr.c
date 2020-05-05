#include "sema.h"

#include <assert.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"
#include "table.h"
#include "type.h"
#include "util.h"
#include "var.h"

static const Type *tyNumTable[] = {&tyChar, &tyShort, &tyInt, &tyLong, &tyEnum};

Scope *curscope;

// Call before accessing struct member to ensure that struct is declared.
void ensure_struct(Type *type, const Token *token) {
  assert(type->kind == TY_STRUCT);
  if (type->struct_.info == NULL) {
    StructInfo *sinfo = find_struct(type->struct_.name);
    if (sinfo == NULL)
      parse_error(token, "Accessing unknown struct(%.*s)'s member", type->struct_.name->bytes,
                  type->struct_.name->chars);
    type->struct_.info = sinfo;
  }
}

bool can_cast(const Type *dst, const Type *src, Expr *src_expr, bool is_explicit) {
  if (same_type(dst, src))
    return true;

  if (dst->kind == TY_VOID)
    return src->kind == TY_VOID || is_explicit;
  if (src->kind == TY_VOID)
    return false;

  switch (dst->kind) {
  case TY_NUM:
    switch (src->kind) {
    case TY_NUM:
      return true;
    case TY_PTR:
    case TY_ARRAY:
    case TY_FUNC:
      if (is_explicit) {
        // TODO: Check sizeof(long) is same as sizeof(ptr)
        return true;
      }
      break;
    default:
      break;
    }
    break;
  case TY_PTR:
    switch (src->kind) {
    case TY_NUM:
      if (src_expr->kind == EX_NUM &&
          src_expr->num.ival == 0)  // Special handling for 0 to pointer.
        return true;
      if (is_explicit)
        return true;
      break;
    case TY_PTR:
      if (is_explicit)
        return true;
      // void* is interchangable with any pointer type.
      if (dst->pa.ptrof->kind == TY_VOID || src->pa.ptrof->kind == TY_VOID)
        return true;
      if (src->pa.ptrof->kind == TY_FUNC)
        return can_cast(dst, src->pa.ptrof, src_expr, is_explicit);
      break;
    case TY_ARRAY:
      if (is_explicit)
        return true;
      if (same_type(dst->pa.ptrof, src->pa.ptrof) ||
          can_cast(dst, ptrof(src->pa.ptrof), src_expr, is_explicit))
        return true;
      break;
    case TY_FUNC:
      if (is_explicit)
        return true;
      if (dst->pa.ptrof->kind == TY_FUNC) {
        const Type *ftype = dst->pa.ptrof;
        return (same_type(ftype, src) ||
                (ftype->func.param_types == NULL || src->func.param_types == NULL));
      }
      break;
    default:  break;
    }
    break;
  case TY_ARRAY:
    switch (src->kind) {
    case TY_PTR:
      if (is_explicit && same_type(dst->pa.ptrof, src->pa.ptrof))
        return true;
      // Fallthrough
    case TY_ARRAY:
      if (is_explicit)
        return true;
      break;
    default:  break;
    }
    break;
  default:
    break;
  }
  return false;
}

static bool check_cast(const Type *dst, const Type *src, Expr *src_expr, bool is_explicit,
                       const Token *token) {
  if (!can_cast(dst, src, src_expr, is_explicit)) {
    parse_error(token, "Cannot convert value from type %d to %d", src->kind, dst->kind);
    return false;
  }
  if (dst->kind == TY_ARRAY) {
    parse_error(token, "Cannot cast to array type");
    return false;
  }
  return true;
}

Expr *make_cast(const Type *type, const Token *token, Expr *sub, bool is_explicit) {
  if (type->kind == TY_VOID || sub->type->kind == TY_VOID)
    parse_error(NULL, "cannot use `void' as a value");

  if (same_type(type, sub->type))
    return sub;
  //if (is_const(sub)) {
  //  // Casting number types needs its value range info,
  //  // so handlded in codegen.
  //  sub->type = type;
  //  return sub;
  //}

  check_cast(type, sub->type, sub, is_explicit, token);

  return new_expr_cast(type, token, sub);
}

// num +|- num
static Expr *add_num(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs, bool keep_left) {
  const Type *ltype = lhs->type;
  const Type *rtype = rhs->type;
  assert(ltype->kind == TY_NUM && rtype->kind == TY_NUM);
  enum NumKind lnt = ltype->num.kind;
  enum NumKind rnt = rtype->num.kind;
  if (lnt == NUM_ENUM)
    lnt = NUM_INT;
  if (rnt == NUM_ENUM)
    rnt = NUM_INT;

  if (is_const(lhs) && is_const(rhs)) {
    intptr_t lval = lhs->num.ival;
    intptr_t rval = rhs->num.ival;
    intptr_t value;
    switch (kind) {
    case EX_ADD: value = lval + rval; break;
    case EX_SUB: value = lval - rval; break;
    default:
      assert(false);
      value = -1;
      break;
    }
    Num num = {value};
    const Type *type = lnt >= rnt ? lhs->type : rhs->type;
    return new_expr_numlit(type, lhs->token, &num);
  }

  const Type *type;
  if (lnt >= rnt || keep_left) {
    type = tyNumTable[lnt];
    rhs = make_cast(type, rhs->token, rhs, false);
  } else {
    type = tyNumTable[rnt];
    lhs = make_cast(type, lhs->token, lhs, false);
  }
  return new_expr_bop(kind, type, tok, lhs, rhs);
}

// pointer +|- num
static Expr *add_ptr_num(enum ExprKind kind, const Token *token, Expr *lhs, Expr *rhs) {
  const Type *ptr_type;
  const Type *ltype = lhs->type;
  if (ltype->kind == TY_PTR || ltype->kind == TY_ARRAY) {
    ptr_type = ltype;
  } else {
    assert(kind != EX_SUB);
    ptr_type = rhs->type;
  }
  if (ptr_type->kind == TY_ARRAY)
    ptr_type = array_to_ptr(ptr_type);
  assert(ptr_type->kind == TY_PTR);
  return new_expr_bop((enum ExprKind)(EX_PTRADD + (kind - EX_ADD)), ptr_type, token, lhs, rhs);
}

static Expr *add_expr_keep_left(const Token *tok, Expr *lhs, Expr *rhs, bool keep_left) {
  const Type *ltype = lhs->type;
  const Type *rtype = rhs->type;

  if (is_number(ltype->kind) && is_number(rtype->kind))
    return add_num(EX_ADD, tok, lhs, rhs, keep_left);

  switch (ltype->kind) {
  case TY_NUM:
    switch (rtype->kind) {
    case TY_PTR: case TY_ARRAY:
      if (!keep_left)
        return add_ptr_num(EX_ADD, tok, lhs, rhs);
      break;
    default:
      break;
    }
    break;

  case TY_PTR: case TY_ARRAY:
    switch (rtype->kind) {
    case TY_NUM:
      return add_ptr_num(EX_ADD, tok, lhs, rhs);
    default:
      break;
    }
    break;

  default:
    break;
  }

  parse_error(tok, "Illegal `+'");
  return NULL;
}

Expr *add_expr(const Token *tok, Expr *lhs, Expr *rhs) {
  return add_expr_keep_left(tok, lhs, rhs, true);
}

static Expr *diff_ptr(const Token *tok, Expr *lhs, Expr *rhs) {
  const Type *ltype = array_to_ptr(lhs->type);
  const Type *rtype = array_to_ptr(rhs->type);
  if (!same_type(ltype, rtype))
    parse_error(tok, "Different pointer diff");
  const Type *elem_type = ltype;
  if (elem_type->kind == TY_PTR)
    elem_type = elem_type->pa.ptrof;
  return new_expr_bop(EX_DIV, &tySize, tok, new_expr_bop(EX_SUB, &tySize, tok, lhs, rhs),
                      new_expr_sizeof(tok, elem_type, NULL));
}

static Expr *sub_expr_keep_left(const Token *tok, Expr *lhs, Expr *rhs, bool keep_left) {
  if (is_number(lhs->type->kind)) {
    if (is_number(rhs->type->kind))
      return add_num(EX_SUB, tok, lhs, rhs, keep_left);
    if (same_type(lhs->type, rhs->type))
      return new_expr_bop(EX_SUB, lhs->type, tok, lhs, rhs);
  }

  switch (lhs->type->kind) {
  case TY_PTR:
    switch (rhs->type->kind) {
    case TY_NUM:
      return add_ptr_num(EX_SUB, tok, lhs, rhs);
    case TY_PTR: case TY_ARRAY:
      return diff_ptr(tok, lhs, rhs);
    default:
      break;
    }
    break;

  case TY_ARRAY:
    switch (rhs->type->kind) {
    case TY_PTR: case TY_ARRAY:
      return diff_ptr(tok, lhs, rhs);
    default:
      break;
    }
    break;

  default:
    break;
  }

  parse_error(tok, "Illegal `-'");
  return NULL;
}

static bool cast_numbers(Expr **pLhs, Expr **pRhs, bool keep_left) {
  Expr *lhs = *pLhs;
  Expr *rhs = *pRhs;
  const Type *ltype = lhs->type;
  const Type *rtype = rhs->type;
  if (!is_number(ltype->kind) || !is_number(rtype->kind))
    return false;

  enum NumKind lkind = ltype->num.kind;
  enum NumKind rkind = rtype->num.kind;
  if (lkind == NUM_ENUM)
    lkind = NUM_INT;
  if (rkind == NUM_ENUM)
    rkind = NUM_INT;
  if (lkind != rkind) {
    if (lkind > rkind || keep_left)
      *pRhs = make_cast(ltype, rhs->token, rhs, false);
    else if (lkind < rkind)
      *pLhs = make_cast(rtype, lhs->token, lhs, false);
  }
  return true;
}

const VarInfo *search_from_anonymous(const Type *type, const Name *name, const Token *ident, Vector *stack) {
  assert(type->kind == TY_STRUCT);
  ensure_struct((Type*)type, ident);

  const Vector *members = type->struct_.info->members;
  for (int i = 0, len = members->len; i < len; ++i) {
    const VarInfo *member = members->data[i];
    if (member->name != NULL) {
      if (equal_name(member->name, name)) {
        vec_push(stack, (void*)(long)i);
        return member;
      }
    } else if (member->type->kind == TY_STRUCT) {
      vec_push(stack, (void*)(intptr_t)i);
      const VarInfo *submember = search_from_anonymous(member->type, name, ident, stack);
      if (submember != NULL)
        return submember;
      vec_pop(stack);
    }
  }
  return NULL;
}

static enum ExprKind swap_cmp(enum ExprKind kind) {
  assert(EX_EQ <= kind && kind <= EX_GT);
  if (kind >= EX_LT)
    kind = EX_GT - (kind - EX_LT);
  return kind;
}

static Expr *sema_cmp(Expr *expr) {
  Expr *lhs = expr->bop.lhs, *rhs = expr->bop.rhs;
  if (lhs->type->kind == TY_PTR || rhs->type->kind == TY_PTR) {
    if (lhs->type->kind != TY_PTR) {
      Expr *tmp = lhs;
      lhs = rhs;
      rhs = tmp;
      expr->kind = swap_cmp(expr->kind);
    }
    const Type *lt = lhs->type, *rt = rhs->type;
    if (!can_cast(lt, rt, rhs, false))
      parse_error(expr->token, "Cannot compare pointer to other types");
    if (rt->kind != TY_PTR)
      rhs = make_cast(lhs->type, expr->token, rhs, false);
  } else {
    if (!cast_numbers(&lhs, &rhs, false))
      parse_error(expr->token, "Cannot compare except numbers");

    if (is_const(lhs) && !is_const(rhs)) {
      Expr *tmp = lhs;
      lhs = rhs;
      rhs = tmp;
      expr->kind = swap_cmp(expr->kind);
    }
  }
  expr->bop.lhs = lhs;
  expr->bop.rhs = rhs;
  return expr;
}

static void sema_lval(const Token *tok, Expr *expr, const char *error) {
  while (expr->kind == EX_GROUP)
    expr = expr->unary.sub;

  switch (expr->kind) {
  case EX_VARIABLE:
  case EX_DEREF:
  case EX_MEMBER:
    break;
  default:
    parse_error(tok, error);
    break;
  }
}

// Traverse expr to check semantics and determine value type.
static Expr *sema_expr_keep_left(Expr *expr, bool keep_left) {
  if (expr == NULL)
    return NULL;

  switch (expr->kind) {
  // Literals
  case EX_NUM:
  case EX_STR:
    assert(expr->type != NULL);
    break;

  case EX_VARIABLE:
    {
      const Name *name = expr->variable.name;
      const Type *type = NULL;
      Scope *scope = NULL;
      if (curscope != NULL) {
        scope = curscope;
        VarInfo *varinfo = scope_find(&scope, name);
        if (varinfo != NULL) {
          if (varinfo->flag & VF_STATIC) {
            // Replace local variable reference to global.
            name = varinfo->local.label;
            expr = new_expr_variable(name, varinfo->type, expr->token);
            scope = NULL;
          } else {
            type = varinfo->type;
          }
        }
      }
      if (type == NULL) {
        VarInfo *varinfo = find_global(name);
        if (varinfo != NULL) {
          type = varinfo->type;
        }
      }
      if (type == NULL) {
        intptr_t value;
        if (find_enum_value(name, &value)) {
          Num num = {.ival = value};
          return new_expr_numlit(&tyInt, NULL, &num);
        }
      }
      if (type == NULL)
        parse_error(expr->token, "Undefined `%.*s'", name->bytes, name->chars);
      expr->type = type;
      expr->variable.scope = scope;
    }
    break;

  // Binary operators
  case EX_ADD:
  case EX_SUB:
  case EX_MUL:
  case EX_DIV:
  case EX_MOD:
  case EX_BITAND:
  case EX_BITOR:
  case EX_BITXOR:
  case EX_LSHIFT:
  case EX_RSHIFT:
  case EX_EQ:
  case EX_NE:
  case EX_LT:
  case EX_GT:
  case EX_LE:
  case EX_GE:
  case EX_LOGAND:
  case EX_LOGIOR:
  case EX_ASSIGN:
  case EX_COMMA:
    expr->bop.lhs = sema_expr(expr->bop.lhs);
    expr->bop.rhs = sema_expr(expr->bop.rhs);
    assert(expr->bop.lhs->type != NULL);
    assert(expr->bop.rhs->type != NULL);

    switch (expr->kind) {
    case EX_ADD:
      return add_expr_keep_left(expr->token, expr->bop.lhs, expr->bop.rhs, keep_left);
    case EX_SUB:
      return sub_expr_keep_left(expr->token, expr->bop.lhs, expr->bop.rhs, keep_left);
    case EX_MUL:
    case EX_DIV:
    case EX_MOD:
    case EX_BITAND:
    case EX_BITOR:
    case EX_BITXOR:
      if (!cast_numbers(&expr->bop.lhs, &expr->bop.rhs, keep_left))
        parse_error(expr->token, "Cannot use `%d' except numbers.", expr->kind);

      if (is_const(expr->bop.lhs) && is_const(expr->bop.rhs)) {
        Expr *lhs = expr->bop.lhs, *rhs = expr->bop.rhs;
        intptr_t lval = lhs->num.ival;
        intptr_t rval = rhs->num.ival;
        intptr_t value;
        switch (expr->kind) {
        case EX_MUL:     value = lval * rval; break;
        case EX_DIV:     value = lval / rval; break;
        case EX_MOD:     value = lval % rval; break;
        case EX_BITAND:  value = lval & rval; break;
        case EX_BITOR:   value = lval | rval; break;
        case EX_BITXOR:  value = lval ^ rval; break;
        default:
          assert(!"err");
          value = -1;  // Dummy
          break;
        }
        Num num = {value};
        const Type *type = lhs->type->num.kind >= rhs->type->num.kind ? lhs->type : rhs->type;
        return new_expr_numlit(type, lhs->token, &num);
      }

      expr->type = expr->bop.lhs->type;
      break;

    case EX_LSHIFT:
    case EX_RSHIFT:
      {
        enum TypeKind k;
        if (!is_number(k = expr->bop.lhs->type->kind) ||
            !is_number(k = expr->bop.rhs->type->kind))
          parse_error(expr->token, "Cannot use `%d' except numbers.", k);

        if (is_const(expr->bop.lhs) && is_const(expr->bop.rhs)) {
          intptr_t lval = expr->bop.lhs->num.ival;
          intptr_t rval = expr->bop.rhs->num.ival;
          intptr_t value = expr->kind == EX_LSHIFT ? lval << rval : lval >> rval;
          Num num = {value};
          return new_expr_numlit(expr->bop.lhs->type, expr->bop.lhs->token, &num);
        }

        expr->type = expr->bop.lhs->type;
      }
      break;

    case EX_EQ:
    case EX_NE:
    case EX_LT:
    case EX_GT:
    case EX_LE:
    case EX_GE:
      expr = sema_cmp(expr);
      break;

    case EX_LOGAND:
    case EX_LOGIOR:
      break;

    case EX_ASSIGN:
      if (expr->bop.lhs->kind == EX_GROUP)
        parse_error(expr->token, "Cannot assign");
      sema_lval(expr->token, expr->bop.lhs, "Cannot assign");
      switch (expr->bop.lhs->type->kind) {
      case TY_ARRAY:
        parse_error(expr->token, "Cannot assign to array");
        break;
      case TY_FUNC:
        parse_error(expr->token, "Cannot assign to function");
        break;
      default: break;
      }
      expr->type = expr->bop.lhs->type;
      expr->bop.rhs = make_cast(expr->type, expr->token, expr->bop.rhs, false);
      break;

    case EX_COMMA:
      expr->type = expr->bop.rhs->type;
      break;

    default:
      fprintf(stderr, "expr kind=%d\n", expr->kind);
      assert(!"sema not handled!");
      break;
    }
    break;

  // Unary operators
  case EX_POS:
  case EX_NEG:
  case EX_NOT:
  case EX_BITNOT:
  case EX_PREINC:
  case EX_PREDEC:
  case EX_POSTINC:
  case EX_POSTDEC:
  case EX_REF:
  case EX_DEREF:
  case EX_GROUP:
  case EX_CAST:
  case EX_ASSIGN_WITH:
    expr->unary.sub = sema_expr_keep_left(expr->unary.sub, expr->kind == EX_ASSIGN_WITH);
    assert(expr->unary.sub->type != NULL);

    switch (expr->kind) {
    case EX_POS:
      if (!is_number(expr->unary.sub->type->kind))
        parse_error(expr->token, "Cannot apply `+' except number types");
      return expr->unary.sub;

    case EX_NEG:
      if (!is_number(expr->unary.sub->type->kind))
        parse_error(expr->token, "Cannot apply `-' except number types");
      if (is_const(expr->unary.sub)) {
        Expr *sub = expr->unary.sub;
        sub->num.ival = -sub->num.ival;
        return sub;
      }
      expr->type = expr->unary.sub->type;
      break;

    case EX_NOT:
      switch (expr->unary.sub->type->kind) {
      case TY_NUM:
      case TY_PTR:
      case TY_ARRAY:
        break;
      default:
        parse_error(expr->token, "Cannot apply `!' except number or pointer types");
        break;
      }
      break;

    case EX_BITNOT:
      switch (expr->unary.sub->type->kind) {
      case TY_NUM:
        expr->type = expr->unary.sub->type;
        break;
      default:
        parse_error(expr->token, "Cannot apply `~' except number type");
        break;
      }
      break;

    case EX_PREINC:
    case EX_PREDEC:
    case EX_POSTINC:
    case EX_POSTDEC:
      expr->type = expr->unary.sub->type;
      break;

    case EX_REF:
      sema_lval(expr->token, expr->unary.sub, "Cannot take reference");
      expr->type = ptrof(expr->unary.sub->type);
      break;

    case EX_DEREF:
      {
        Expr *sub = expr->unary.sub;
        switch (sub->type->kind) {
        case TY_PTR:
        case TY_ARRAY:
          expr->type = sub->type->pa.ptrof;
          break;
        case TY_FUNC:
          expr->type = sub->type;
          break;
        default:
          parse_error(expr->token, "Cannot dereference raw type");
          break;
        }
      }
      break;

    case EX_GROUP:
      if (is_const(expr->unary.sub))
        return expr->unary.sub;
      expr->type = expr->unary.sub->type;
      break;

    case EX_ASSIGN_WITH:
      sema_lval(expr->token, expr->unary.sub->bop.lhs, "Cannot assign");
      expr->type = expr->unary.sub->bop.lhs->type;
      break;

    case EX_CAST:
      {
        Expr *sub = expr->unary.sub;
        check_cast(expr->type, sub->type, sub, true, expr->token);
        if (same_type(expr->type, sub->type))
          return sub;
      }
      break;

    default:
      fprintf(stderr, "expr kind=%d\n", expr->kind);
      assert(!"sema not handled!");
      break;
    }
    break;

  case EX_TERNARY:
    {
      expr->ternary.cond = sema_expr(expr->ternary.cond);
      Expr *tval = sema_expr(expr->ternary.tval);
      Expr *fval = sema_expr(expr->ternary.fval);
      const Type *ttype = tval->type;
      const Type *ftype = fval->type;
      if (ttype->kind == TY_ARRAY) {
        ttype = array_to_ptr(ttype);
        tval = new_expr_cast(ttype, tval->token, tval);
      }
      if (ftype->kind == TY_ARRAY) {
        ftype = array_to_ptr(ftype);
        fval = new_expr_cast(ftype, fval->token, fval);
      }
      expr->ternary.tval = tval;
      expr->ternary.fval = fval;

      if (same_type(ttype, ftype)) {
        expr->type = ttype;
        break;
      }
      if (is_void_ptr(ttype) && ftype->kind == TY_PTR) {
        expr->type = ftype;
        break;
      }
      if (is_void_ptr(ftype) && ttype->kind == TY_PTR) {
        expr->type = ttype;
        break;
      }
      if (is_number(ttype->kind) && is_number(ftype->kind)) {
        if (ttype->num.kind > ftype->num.kind) {
          expr->type = ttype;
          expr->ternary.fval = new_expr_cast(ttype, fval->token, fval);
        } else {
          expr->type = ftype;
          expr->ternary.tval = new_expr_cast(ftype, tval->token, tval);
        }
        break;
      }

      parse_error(NULL, "lhs and rhs must be same type");
    }
    break;

  case EX_MEMBER:  // x.member or x->member
    {
      Expr *target = expr->member.target;
      expr->member.target = target = sema_expr(target);
      assert(target->type != NULL);

      const Token *acctok = expr->token;
      const Token *ident = expr->member.ident;
      const Name *name = ident->ident;

      // Find member's type from struct info.
      const Type *targetType = target->type;
      if (acctok->kind == TK_DOT) {
        if (targetType->kind != TY_STRUCT)
          parse_error(acctok, "`.' for non struct value");
      } else {  // TK_ARROW
        if (targetType->kind == TY_PTR)
          targetType = targetType->pa.ptrof;
        else if (targetType->kind == TY_ARRAY)
          targetType = targetType->pa.ptrof;
        else
          parse_error(acctok, "`->' for non pointer value");
        if (targetType->kind != TY_STRUCT)
          parse_error(acctok, "`->' for non struct value");
      }

      ensure_struct((Type*)targetType, ident);
      int index = var_find(targetType->struct_.info->members, name);
      if (index >= 0) {
        const VarInfo *member = targetType->struct_.info->members->data[index];
        expr->type = member->type;
        expr->member.index = index;
      } else {
        Vector *stack = new_vector();
        const VarInfo *member = search_from_anonymous(targetType, ident->ident, ident, stack);
        if (member == NULL)
          parse_error(ident, "`%.*s' doesn't exist in the struct", name->bytes, name->chars);
        Expr *p = target;
        const Type *type = targetType;
        for (int i = 0; i < stack->len; ++i) {
          int index = (int)(long)stack->data[i];
          const VarInfo *member = type->struct_.info->members->data[index];
          type = member->type;
          p = new_expr_member(acctok, type, p, NULL, index);
        }
        expr = p;
      }
    }
    break;

  case EX_SIZEOF:
    {
      Expr *sub = expr->sizeof_.sub;
      if (sub != NULL) {
        sub = sema_expr(sub);
        assert(sub->type != NULL);
        expr->sizeof_.type = sub->type;
      }
    }
    break;

  case EX_FUNCALL:
    {
      Expr *func = expr->funcall.func;
      Vector *args = expr->funcall.args;  // <Expr*>
      expr->funcall.func = func = sema_expr(func);
      if (args != NULL) {
        for (int i = 0, len = args->len; i < len; ++i)
          args->data[i] = sema_expr(args->data[i]);
      }

      const Type *functype;
      if (!((functype = func->type)->kind == TY_FUNC ||
            (func->type->kind == TY_PTR && (functype = func->type->pa.ptrof)->kind == TY_FUNC)))
        parse_error(NULL, "Cannot call except funtion");
      expr->type = functype->func.ret;

      Vector *param_types = functype->func.param_types;  // <const Type*>
      bool vaargs = functype->func.vaargs;
      if (param_types != NULL) {
        int argc = args != NULL ? args->len : 0;
        int paramc = param_types->len;
        if (!(argc == paramc ||
              (vaargs && argc >= paramc)))
          parse_error(func->token, "function `%.*s' expect %d arguments, but %d", func->variable.name->bytes, func->variable.name->chars, paramc, argc);
      }

      if (args != NULL && param_types != NULL) {
        int paramc = param_types->len;
        for (int i = 0, len = args->len; i < len; ++i) {
          if (i < param_types->len) {
            Expr *arg = args->data[i];
            const Type *type = (const Type*)param_types->data[i];
            args->data[i] = make_cast(type, arg->token, arg, false);
          } else if (vaargs && i >= paramc) {
            Expr *arg = args->data[i];
            const Type *type = arg->type;
            if (type->kind == TY_NUM && type->num.kind < NUM_INT)  // Promote variadic argument.
              args->data[i] = make_cast(&tyInt, arg->token, arg, false);
          }
        }
      }
    }
    break;

  default:
    fprintf(stderr, "expr kind=%d\n", expr->kind);
    assert(!"sema not handled!");
    break;
  }

  assert(expr->type != NULL);
  return expr;
}

Expr *sema_expr(Expr *expr) {
  return sema_expr_keep_left(expr, false);
}
