#pragma once

#include <stdbool.h>
#include <stddef.h>  // size_t

#include "ast.h"  // ExprKind

typedef struct Scope Scope;
typedef struct Token Token;
typedef struct Type Type;
typedef struct VarInfo VarInfo;

VarInfo *str_to_char_array(Scope *scope, const Type *type, Initializer *init, Vector *toplevel);
Expr *str_to_char_array_var(Scope *scope, Expr *str, Vector *toplevel);
bool check_cast(const Type *dst, const Type *src, bool zero, bool is_explicit, const Token *token);
Expr *make_cast(const Type *type, const Token *token, Expr *sub, bool is_explicit);
bool cast_numbers(Expr **pLhs, Expr **pRhs, bool keep_left);
void check_lval(const Token *tok, Expr *expr, const char *error);
Expr *make_refer(const Token *tok, Expr *expr);
Expr *new_expr_num_bop(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs, bool keep_left);
Expr *new_expr_int_bop(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs, bool keep_left);
Expr *new_expr_addsub(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs, bool keep_left);
Expr *new_expr_incdec(enum ExprKind kind, const Token *tok, Expr *sub);
Expr *new_expr_cmp(enum ExprKind kind, const Token *tok, Expr *lhs, Expr *rhs);
Expr *make_cond(Expr *expr);
Expr *make_not_cond(Expr *expr);
const Type *get_callee_type(Expr *func);
void check_funcall_args(Expr *func, Vector *args, Scope *scope, Vector *toplevel);

void fix_array_size(Type *type, Initializer *init);
Stmt *build_memcpy(Scope *scope, Expr *dst, Expr *src, size_t size);
