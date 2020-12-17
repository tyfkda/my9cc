#pragma once

typedef struct Expr Expr;
typedef struct Name Name;
typedef struct Stmt Stmt;
typedef struct Type Type;
typedef struct Token Token;
typedef struct VarInfo VarInfo;
typedef struct Vector Vector;

// Initializer

enum InitializerKind {
  IK_SINGLE,  // 123
  IK_MULTI,   // {...}
  IK_DOT,     // .x=123
  IK_ARR,     // [n]=123
};

typedef struct Initializer {
  enum InitializerKind kind;
  const Token *token;
  union {
    Expr *single;
    Vector *multi;  // <Initializer*>
    struct {
      const Name *name;
      struct Initializer *value;
    } dot;
    struct {
      Expr *index;
      struct Initializer *value;
    } arr;
  };
} Initializer;

void fix_array_size(Type *type, Initializer *init);
VarInfo *str_to_char_array(const Type *type, Initializer *init);
Expr *str_to_char_array_var(Expr *str);
Initializer *convert_str_to_ptr_initializer(const Type *type, Initializer *init);
Stmt *init_char_array_by_string(Expr *dst, Initializer *src);
Initializer *flatten_initializer(const Type *type, Initializer *init);
Initializer *check_global_initializer(const Type *type, Initializer *init);
