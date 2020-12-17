// Parser

#pragma once

#include <stdbool.h>

typedef struct Expr Expr;
typedef struct Function Function;
typedef struct Initializer Initializer;
typedef struct Name Name;
typedef struct Scope Scope;
typedef struct Token Token;
typedef struct Type Type;
typedef struct VarInfo VarInfo;
typedef struct Vector Vector;

extern Function *curfunc;
extern Scope *curscope;
extern Vector *toplevel;  // <Declaration*>

void parse(Vector *toplevel);  // <Declaraion*>

//

const Type *parse_raw_type(int *pflag);
const Type *parse_type_modifier(const Type *type);
const Type *parse_type_suffix(const Type *type);
const Type *parse_full_type(int *pflag, Token **pident);

Vector *parse_args(Token **ptoken);
Vector *parse_funparams(bool *pvaargs);  // Vector<VarInfo*>, NULL=>old style.
bool parse_var_def(const Type **prawType, const Type **ptype, int *pflag, Token **pident);
Vector *extract_varinfo_types(Vector *params);  // <VarInfo*> => <Type*>
Expr *parse_const(void);
Expr *parse_assign(void);
Expr *parse_expr(void);

void not_void(const Type *type, const Token *token);
void not_const(const Type *type, const Token *token);
bool check_cast(const Type *dst, const Type *src, bool zero, bool is_explicit, const Token *token);
Expr *make_cast(const Type *type, const Token *token, Expr *sub, bool is_explicit);
Expr *make_cond(Expr *expr);

Vector *assign_initial_value(Expr *expr, Initializer *init, Vector *inits);
