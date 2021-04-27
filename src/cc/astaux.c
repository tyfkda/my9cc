#include "astaux.h"

#include <assert.h>
#include <limits.h>  // CHAR_BITS
#include <stdlib.h>  // malloc

#include "ast.h"
#include "lexer.h"
#include "table.h"
#include "type.h"
#include "util.h"
#include "var.h"

// Returns created global variable info.
VarInfo *str_to_char_array(Scope *scope, const Type *type, Initializer *init, Vector *toplevel) {
  assert(type->kind == TY_ARRAY && is_char_type(type->pa.ptrof));
  const Token *ident = alloc_ident(alloc_label(), NULL, NULL);
  VarInfo *varinfo = scope_add(scope, ident, type, VS_STATIC);
  if (is_global_scope(scope)) {
    Vector *decls = new_vector();
    vec_push(decls, new_vardecl(varinfo->type, ident, init, varinfo->storage));
    vec_push(toplevel, new_decl_vardecl(decls));
    varinfo->global.init = init;
  } else {
    varinfo->static_.gvar->global.init = init;
  }
  return varinfo;
}

Expr *str_to_char_array_var(Scope *scope, Expr *str, Vector *toplevel) {
  if (str->kind != EX_STR)
    return str;
  const Type* type = str->type;
  Initializer *init = malloc(sizeof(*init));
  init->kind = IK_SINGLE;
  init->single = str;
  init->token = str->token;

  VarInfo *varinfo = str_to_char_array(scope, type, init, toplevel);
  return new_expr_variable(varinfo->name, type, str->token, scope);
}

bool check_cast(const Type *dst, const Type *src, bool zero, bool is_explicit, const Token *token) {
  bool ok = can_cast(dst, src, zero, is_explicit);
  if (!ok || dst->kind == TY_ARRAY) {
    if (token == NULL)
      token = fetch_token();
    fprintf(stderr, "%s(%d): ", token->line->filename, token->line->lineno);

    fprintf(stderr, "Cannot convert value from type `");
    print_type(stderr, src);
    fprintf(stderr, "' to %s`", dst->kind == TY_ARRAY ? "array type " : "");
    print_type(stderr, dst);
    fprintf(stderr, "'\n");
    parse_error(token, NULL);
    return false;
  }
  return true;
}

Expr *make_cast(const Type *type, const Token *token, Expr *sub, bool is_explicit) {
  if (type->kind == TY_VOID || sub->type->kind == TY_VOID)
    parse_error(NULL, "cannot use `void' as a value");

  if (same_type(type, sub->type))
    return sub;
  if (is_const(sub) && is_number(sub->type) && is_number(type)) {
#ifndef __NO_FLONUM
    switch (sub->type->kind) {
    case TY_FLONUM:
      if (type->kind == TY_FIXNUM) {
        Fixnum fixnum = sub->flonum;
        return new_expr_fixlit(type, sub->token, fixnum);
      }
      sub->type = type;
      return sub;
    case TY_FIXNUM:
      if (type->kind == TY_FLONUM) {
        double flonum = sub->fixnum;
        return new_expr_flolit(type, sub->token, flonum);
      }
      break;
    default:
      break;
    }
#endif

    {
      int bytes = type_size(type);
      int src_bytes = type_size(sub->type);
      if (bytes < (int)type_size(&tySize) &&
          (bytes < src_bytes ||
           (bytes == src_bytes &&
            type->fixnum.is_unsigned != sub->type->fixnum.is_unsigned))) {
        int bits = bytes * CHAR_BIT;
        UFixnum mask = (-1UL) << bits;
        Fixnum value = sub->fixnum;
        if (!type->fixnum.is_unsigned &&  // signed
            (value & (1UL << (bits - 1))))  // negative
          value |= mask;
        else
          value &= ~mask;
        sub->fixnum = value;
      }
    }
    sub->type = type;
    return sub;
  }

  check_cast(type, sub->type, is_zero(sub), is_explicit, token);
  if (sub->kind == EX_CAST) {
    sub->type = type;
    return sub;
  }

  return new_expr_cast(type, token, sub);
}

bool cast_numbers(Expr **pLhs, Expr **pRhs, bool keep_left) {
  Expr *lhs = *pLhs;
  Expr *rhs = *pRhs;
  const Type *ltype = lhs->type;
  const Type *rtype = rhs->type;
  assert(ltype != NULL);
  assert(rtype != NULL);
  if (!is_number(ltype)) {
    parse_error(lhs->token, "number type expected");
    return false;
  }
  if (!is_number(rtype)) {
    parse_error(rhs->token, "number type expected");
    return false;
  }

#ifndef __NO_FLONUM
  {
    bool lflo = is_flonum(ltype), rflo = is_flonum(rtype);
    if (lflo || rflo) {
      int dir = !lflo ? 1 : !rflo ? -1 : (int)rtype->flonum.kind - (int)ltype->flonum.kind;
      if (dir < 0 || keep_left)
        *pRhs = make_cast(ltype, rhs->token, rhs, false);
      else if (dir > 0)
        *pLhs = make_cast(rtype, lhs->token, lhs, false);
      return true;
    }
  }
#endif
  enum FixnumKind lkind = ltype->fixnum.kind;
  enum FixnumKind rkind = rtype->fixnum.kind;
  if (ltype->fixnum.kind == FX_ENUM) {
    ltype = &tyInt;
    lkind = FX_INT;
  }
  if (rtype->fixnum.kind == FX_ENUM) {
    rtype = &tyInt;
    rkind = FX_INT;
  }

  int l = (lkind << 1) | (ltype->fixnum.is_unsigned ? 1 : 0);
  int r = (rkind << 1) | (rtype->fixnum.is_unsigned ? 1 : 0);
  if (keep_left || l > r)
    *pRhs = make_cast(ltype, rhs->token, rhs, false);
  else if (l < r)
    *pLhs = make_cast(rtype, lhs->token, lhs, false);
  return true;
}

void check_lval(const Token *tok, Expr *expr, const char *error) {
  switch (expr->kind) {
  case EX_VAR:
  case EX_DEREF:
  case EX_MEMBER:
    break;
  default:
    parse_error(tok, error);
    break;
  }
}

static void check_referable(const Token *tok, Expr *expr, const char *error) {
  if (expr->kind == EX_COMPLIT)
    return;
  check_lval(tok, expr, error);
}

Expr *make_refer(const Token *tok, Expr *expr) {
  check_referable(tok, expr, "Cannot take reference");
  if (expr->kind == EX_DEREF)
    return expr->unary.sub;
  if (expr->kind == EX_VAR) {
    VarInfo *varinfo = scope_find(expr->var.scope, expr->var.name, NULL);
    assert(varinfo != NULL);
    varinfo->storage |= VS_REF_TAKEN;
    if ((varinfo->storage & VS_STATIC) != 0 && !is_global_scope(expr->var.scope)) {
      VarInfo *gvarinfo = varinfo->static_.gvar;
      gvarinfo->storage |= VS_REF_TAKEN;
    }
  }
  return new_expr_unary(EX_REF, ptrof(expr->type), tok, expr);
}

Expr *new_expr_num_bop(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs, bool keep_left) {
  if (is_const(lhs) && is_number(lhs->type) &&
      is_const(rhs) && is_number(rhs->type)) {
#ifndef __NO_FLONUM
    if (is_flonum(lhs->type) || is_flonum(rhs->type)) {
      double lval = is_flonum(lhs->type) ? lhs->flonum : lhs->fixnum;
      double rval = is_flonum(rhs->type) ? rhs->flonum : rhs->fixnum;
      double value;
      switch (kind) {
      case EX_MUL:     value = lval * rval; break;
      case EX_DIV:     value = lval / rval; break;
      default:
        assert(!"err");
        value = -1;  // Dummy
        break;
      }
      const Type *type = lhs->type;
      if (!keep_left && is_flonum(rhs->type))
        type = rhs->type;
      if (is_flonum(type)) {
        return new_expr_flolit(type, lhs->token, value);
      } else {
        Fixnum fixnum = value;
        return new_expr_fixlit(type, lhs->token, fixnum);
      }
    }
#endif

#define CALC(kind, l, r, value) \
  switch (kind) { \
  default: assert(false); /* Fallthrough */ \
  case EX_MUL:     value = l * r; break; \
  case EX_DIV:     value = l / r; break; \
  case EX_MOD:     value = l % r; break; \
  case EX_BITAND:  value = l & r; break; \
  case EX_BITOR:   value = l | r; break; \
  case EX_BITXOR:  value = l ^ r; break; \
  }

    Fixnum value;
    if (lhs->type->fixnum.is_unsigned) {
      UFixnum l = lhs->fixnum;
      UFixnum r = rhs->fixnum;
      CALC(kind, l, r, value)
    } else {
      Fixnum l = lhs->fixnum;
      Fixnum r = rhs->fixnum;
      CALC(kind, l, r, value)
    }
#undef CALC
    const Type *type = keep_left || lhs->type->fixnum.kind >= rhs->type->fixnum.kind ? lhs->type : rhs->type;
    return new_expr_fixlit(type, lhs->token, value);
  }

  cast_numbers(&lhs, &rhs, keep_left);
  return new_expr_bop(kind, lhs->type, tok, lhs, rhs);
}

Expr *new_expr_int_bop(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs, bool keep_left) {
  if (!is_fixnum(lhs->type->kind))
    parse_error(lhs->token, "int type expected");
  if (!is_fixnum(rhs->type->kind))
    parse_error(rhs->token, "int type expected");
  return new_expr_num_bop(kind, tok, lhs, rhs, keep_left);
}

Expr *new_expr_addsub(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs, bool keep_left) {
  const Type *type = NULL;
  const Type *ltype = lhs->type;
  const Type *rtype = rhs->type;
  assert(ltype != NULL);
  assert(rtype != NULL);
  if (is_number(ltype) && is_number(rtype)) {
    if (is_const(lhs) && is_const(rhs)) {
#ifndef __NO_FLONUM
      if (is_flonum(lhs->type) || is_flonum(rhs->type)) {
        double lval = is_flonum(lhs->type) ? lhs->flonum : lhs->fixnum;
        double rval = is_flonum(rhs->type) ? rhs->flonum : rhs->fixnum;
        double value;
        switch (kind) {
        case EX_ADD:     value = lval + rval; break;
        case EX_SUB:     value = lval - rval; break;
        default:
          assert(!"err");
          value = -1;  // Dummy
          break;
        }
        const Type *type = lhs->type;
        if (!keep_left && is_flonum(rhs->type))
          type = rhs->type;
        if (is_flonum(type)) {
          return new_expr_flolit(type, lhs->token, value);
        } else {
          Fixnum fixnum = value;
          return new_expr_fixlit(type, lhs->token, fixnum);
        }
      }
#endif
      enum FixnumKind lnt = ltype->fixnum.kind;
      enum FixnumKind rnt = rtype->fixnum.kind;
      if (lnt == FX_ENUM)
        lnt = FX_INT;
      if (rnt == FX_ENUM)
        rnt = FX_INT;

      Fixnum lval = lhs->fixnum;
      Fixnum rval = rhs->fixnum;
      Fixnum value;
      switch (kind) {
      case EX_ADD: value = lval + rval; break;
      case EX_SUB: value = lval - rval; break;
      default:
        assert(false);
        value = -1;
        break;
      }
      const Type *type = lnt >= rnt ? lhs->type : rhs->type;
      return new_expr_fixlit(type, lhs->token, value);
    }

    cast_numbers(&lhs, &rhs, keep_left);
    type = lhs->type;
  } else if (ptr_or_array(ltype)) {
    if (is_fixnum(rtype->kind)) {
      type = ltype;
      if (ltype->kind == TY_ARRAY)
        type = array_to_ptr(ltype);
      // lhs + ((size_t)rhs * sizeof(*lhs))
      rhs = new_expr_num_bop(EX_MUL, rhs->token,
                             make_cast(&tySize, rhs->token, rhs, false),
                             new_expr_fixlit(&tySize, tok, type_size(type->pa.ptrof)), false);
    } else if (kind == EX_SUB && ptr_or_array(rtype)) {
      if (ltype->kind == TY_ARRAY)
        ltype = array_to_ptr(ltype);
      if (rtype->kind == TY_ARRAY)
        rtype = array_to_ptr(rtype);
      if (!same_type(ltype, rtype))
        parse_error(tok, "Different pointer diff");
      // ((size_t)lhs - (size_t)rhs) / sizeof(*lhs)
      return new_expr_bop(EX_DIV, &tySSize, tok,
                          new_expr_bop(EX_SUB, &tySSize, tok, lhs, rhs),
                          new_expr_fixlit(&tySSize, tok, type_size(ltype->pa.ptrof)));
    }
  } else if (ptr_or_array(rtype)) {
    if (kind == EX_ADD && is_fixnum(ltype->kind) && !keep_left) {
      type = rhs->type;
      if (type->kind == TY_ARRAY)
        type = array_to_ptr(type);
      // ((size_t)lhs * sizeof(*rhs)) + rhs
      lhs = new_expr_num_bop(EX_MUL, lhs->token,
                             make_cast(&tySize, lhs->token, lhs, false),
                             new_expr_fixlit(&tySize, tok, type_size(type->pa.ptrof)), false);
    }
  }
  if (type == NULL) {
    parse_error(tok, "Cannot apply `%.*s'", (int)(tok->end - tok->begin), tok->begin);
  }
  return new_expr_bop(kind, type, tok, lhs, rhs);
}

Expr *new_expr_incdec(enum ExprKind kind, const Token *tok, Expr *sub) {
  check_referable(tok, sub, "lvalue expected");
  return new_expr_unary(kind, sub->type, tok, sub);
}

static enum ExprKind swap_cmp(enum ExprKind kind) {
  assert(EX_EQ <= kind && kind <= EX_GT);
  if (kind >= EX_LT)
    kind = EX_GT - (kind - EX_LT);
  return kind;
}

Expr *new_expr_cmp(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs) {
  const Type *lt = lhs->type, *rt = rhs->type;
  if (ptr_or_array(lt) || ptr_or_array(rt)) {
    if (lt->kind == TY_ARRAY) {
      lt = array_to_ptr(lt);
      lhs = make_cast(lt, lhs->token, lhs, false);
    }
    if (rt->kind == TY_ARRAY) {
      rt = array_to_ptr(rt);
      rhs = make_cast(rt, rhs->token, rhs, false);
    }
    if (lt->kind != TY_PTR) {  // For comparison between pointer and 0.
      Expr *tmp = lhs;
      lhs = rhs;
      rhs = tmp;
      const Type *tt = lt;
      lt = rt;
      rt = tt;
      kind = swap_cmp(kind);
    }
    if (!can_cast(lt, rt, is_zero(rhs), false))
      parse_error(tok, "Cannot compare pointer to other types");
    if (rt->kind != TY_PTR)
      rhs = make_cast(lhs->type, rhs->token, rhs, false);
  } else {
    if (!cast_numbers(&lhs, &rhs, false))
      parse_error(tok, "Cannot compare except numbers");

    if (is_const(lhs) && is_const(rhs)) {
#define JUDGE(kind, tf, l, r)  \
switch (kind) { \
default: assert(false); /* Fallthrough */ \
case EX_EQ:  tf = l == r; break; \
case EX_NE:  tf = l != r; break; \
case EX_LT:  tf = l < r; break; \
case EX_LE:  tf = l <= r; break; \
case EX_GE:  tf = l >= r; break; \
case EX_GT:  tf = l > r; break; \
}
      bool tf;
      switch (lhs->kind) {
      default:
        assert(false);
        // Fallthrough to suppress warning.
      case EX_FIXNUM:
        assert(rhs->kind == EX_FIXNUM);
        if (lhs->type->fixnum.is_unsigned) {
          UFixnum l = lhs->fixnum, r = rhs->fixnum;
          JUDGE(kind, tf, l, r);
        } else {
          Fixnum l = lhs->fixnum, r = rhs->fixnum;
          JUDGE(kind, tf, l, r);
        }
        break;
#ifndef __NO_FLONUM
      case EX_FLONUM:
        {
          assert(rhs->kind == EX_FLONUM);
          double l = lhs->flonum, r = rhs->flonum;
          JUDGE(kind, tf, l, r);
        }
        break;
#endif
      }
      return new_expr_fixlit(&tyBool, tok, tf);
#undef JUDGE
    }
  }
  return new_expr_bop(kind, &tyBool, tok, lhs, rhs);
}

//

Expr *make_cond(Expr *expr) {
  switch (expr->kind) {
  case EX_FIXNUM:
    break;
#ifndef __NO_FLONUM
  case EX_FLONUM:
    expr = new_expr_fixlit(&tyBool, expr->token, expr->flonum != 0);
    break;
#endif
  case EX_EQ:
  case EX_NE:
  case EX_LT:
  case EX_LE:
  case EX_GE:
  case EX_GT:
  case EX_LOGAND:
  case EX_LOGIOR:
    break;
  default:
    switch (expr->type->kind) {
    case TY_ARRAY:
    case TY_FUNC:
      expr = new_expr_fixlit(&tyBool, expr->token, true);
      break;
    default:
      {
        Expr *zero = make_cast(expr->type, expr->token, new_expr_fixlit(&tyInt, expr->token, 0), false);
        expr = new_expr_cmp(EX_NE, expr->token, expr, zero);
      }
      break;
    }
    break;
  }
  return expr;
}

Expr *make_not_cond(Expr *expr) {
  Expr *cond = make_cond(expr);
  enum ExprKind kind = cond->kind;
  switch (kind) {
  case EX_FIXNUM:
    cond = new_expr_fixlit(&tyBool, expr->token, cond->fixnum == 0);
    break;
#ifndef __NO_FLONUM
  case EX_FLONUM:
    expr = new_expr_fixlit(&tyBool, expr->token, expr->flonum == 0);
    break;
#endif
  case EX_EQ:
  case EX_NE:
  case EX_LT:
  case EX_LE:
  case EX_GE:
  case EX_GT:
    if (kind <= EX_NE)
      kind = (EX_EQ + EX_NE) - kind;
    else
      kind = EX_LT + ((kind - EX_LT) ^ 2);
    cond->kind = kind;
    break;
  case EX_LOGAND:
  case EX_LOGIOR:
    {
      Expr *lhs = make_not_cond(cond->bop.lhs);
      Expr *rhs = make_not_cond(cond->bop.rhs);
      cond = new_expr_bop((EX_LOGAND + EX_LOGIOR) - kind, &tyBool, expr->token, lhs, rhs);
    }
    break;
  default: assert(false); break;
  }
  return cond;
}

const Type *get_callee_type(Expr *func) {
  const Type *type = func->type;
  if (type->kind == TY_PTR)
    type = type->pa.ptrof;
  if (type->kind != TY_FUNC)
    parse_error(func->token, "Cannot call except function");
  return type;
}

void check_funcall_args(Expr *func, Vector *args, Scope *scope, Vector *toplevel) {
  const Type *functype = get_callee_type(func);

  const Vector *param_types = functype->func.param_types;  // <const Type*>
  bool vaargs = functype->func.vaargs;
  if (param_types != NULL) {
    int argc = args != NULL ? args->len : 0;
    int paramc = param_types->len;
    if (!(argc == paramc ||
          (vaargs && argc >= paramc)))
      parse_error(func->token, "function `%.*s' expect %d arguments, but %d", func->var.name->bytes, func->var.name->chars, paramc, argc);
  }

  if (args != NULL) {
    int paramc = param_types != NULL ? param_types->len : -1;
    for (int i = 0, len = args->len; i < len; ++i) {
      Expr *arg = args->data[i];
      if (arg->type->kind == TY_ARRAY) {
        arg = str_to_char_array_var(scope, arg, toplevel);
        arg = make_cast(array_to_ptr(arg->type), arg->token, arg, false);
      }
      if (i < paramc) {
        const Type *type = param_types->data[i];
        arg = make_cast(type, arg->token, arg, false);
      } else if (vaargs && i >= paramc) {
        const Type *type = arg->type;
        switch (type->kind) {
        case TY_FIXNUM:
          if (type->fixnum.kind < FX_INT)  // Promote variadic argument.
            arg = make_cast(&tyInt, arg->token, arg, false);
          break;
#ifndef __NO_FLONUM
        case TY_FLONUM:
          if (type->flonum.kind < FL_DOUBLE)  // Promote variadic argument.
            arg = make_cast(&tyDouble, arg->token, arg, false);
          break;
#endif
        default: break;
        }
      }
      args->data[i] = arg;
    }
  }
}

//

void fix_array_size(Type *type, Initializer *init) {
  assert(init != NULL);
  assert(type->kind == TY_ARRAY);

  bool is_str = (is_char_type(type->pa.ptrof) &&
                 init->kind == IK_SINGLE &&
                 init->single->kind == EX_STR);
  if (!is_str && init->kind != IK_MULTI) {
    parse_error(init->token, "Error initializer");
  }

  ssize_t arr_len = type->pa.length;
  if (arr_len == -1) {
    if (is_str) {
      type->pa.length = init->single->str.size;
    } else {
      ssize_t index = 0;
      ssize_t max_index = 0;
      for (ssize_t i = 0; i < init->multi->len; ++i) {
        Initializer *init_elem = init->multi->data[i];
        if (init_elem->kind == IK_ARR) {
          assert(init_elem->arr.index->kind == EX_FIXNUM);
          index = init_elem->arr.index->fixnum;
        }
        ++index;
        if (max_index < index)
          max_index = index;
      }
      type->pa.length = max_index;
    }
  } else {
    assert(arr_len > 0);
    assert(!is_str || init->single->kind == EX_STR);
    ssize_t init_len = is_str ? (ssize_t)init->single->str.size : (ssize_t)init->multi->len;
    if (init_len > arr_len)
      parse_error(NULL, "Initializer more than array size");
  }
}

Stmt *build_memcpy(Scope *scope, Expr *dst, Expr *src, size_t size) {
  // assert(!is_global_scope(curscope));
  const Type *charptr_type = ptrof(&tyChar);
  VarInfo *dstvar = scope_add(scope, alloc_ident(alloc_label(), NULL, NULL), charptr_type, 0);
  VarInfo *srcvar = scope_add(scope, alloc_ident(alloc_label(), NULL, NULL), charptr_type, 0);
  VarInfo *sizevar = scope_add(scope, alloc_ident(alloc_label(), NULL, NULL), &tySize, 0);
  Expr *dstexpr = new_expr_variable(dstvar->name, dstvar->type, NULL, scope);
  Expr *srcexpr = new_expr_variable(srcvar->name, srcvar->type, NULL, scope);
  Expr *sizeexpr = new_expr_variable(sizevar->name, sizevar->type, NULL, scope);

  Fixnum size_num_lit = size;
  Expr *size_num = new_expr_fixlit(&tySize, NULL, size_num_lit);

  Fixnum zero = 0;
  Expr *zeroexpr = new_expr_fixlit(&tySize, NULL, zero);

  Vector *stmts = new_vector();
  vec_push(stmts, new_stmt_expr(new_expr_bop(EX_ASSIGN, charptr_type, NULL, dstexpr, dst)));
  vec_push(stmts, new_stmt_expr(new_expr_bop(EX_ASSIGN, charptr_type, NULL, srcexpr, src)));
  vec_push(stmts, new_stmt_for(
      NULL,
      new_expr_bop(EX_ASSIGN, &tySize, NULL, sizeexpr, size_num),    // for (_size = size;
      new_expr_bop(EX_GT, &tyBool, NULL, sizeexpr, zeroexpr),        //      _size > 0;
      new_expr_unary(EX_PREDEC, &tySize, NULL, sizeexpr),            //      --_size)
      new_stmt_expr(                                                 //   *_dst++ = *_src++;
          new_expr_bop(EX_ASSIGN, &tyChar, NULL,
                       new_expr_unary(EX_DEREF, &tyChar, NULL,
                                      new_expr_unary(EX_POSTINC, charptr_type, NULL, dstexpr)),
                       new_expr_unary(EX_DEREF, &tyChar, NULL,
                                      new_expr_unary(EX_POSTINC, charptr_type, NULL, srcexpr))))));
  return new_stmt_block(NULL, stmts, NULL);
}
