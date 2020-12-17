#include "parser.h"

#include <assert.h>
#include <inttypes.h>  // PRIdPTR
#include <stdbool.h>
#include <stdlib.h>  // malloc

#include "ast.h"
#include "initializer.h"
#include "lexer.h"
#include "table.h"
#include "type.h"
#include "util.h"
#include "var.h"

const int LF_BREAK = 1 << 0;
const int LF_CONTINUE = 1 << 0;

Function *curfunc;
static int curloopflag;
static Stmt *curswitch;

static Stmt *parse_stmt(void);

Vector *assign_initial_value(Expr *expr, Initializer *init, Vector *inits) {
  if (init == NULL)
    return inits;

  if (inits == NULL)
    inits = new_vector();

  Initializer *org_init = init;
  init = flatten_initializer(expr->type, init);

  switch (expr->type->kind) {
  case TY_ARRAY:
    switch (init->kind) {
    case IK_MULTI:
      {
        size_t arr_len = expr->type->pa.length;
        assert(arr_len != (size_t)-1);
        if ((size_t)init->multi->len > arr_len)
          parse_error(init->token, "Initializer more than array size");

        assert(!is_global_scope(curscope));
        const Type *ptr_type = array_to_ptr(expr->type);
        VarInfo *ptr_varinfo = scope_add(curscope, alloc_ident(alloc_label(), NULL, NULL), ptr_type, 0);
        Expr *ptr_var = new_expr_variable(ptr_varinfo->name, ptr_type, NULL, curscope);
        vec_push(inits, new_stmt_expr(new_expr_bop(EX_ASSIGN, ptr_type, NULL, ptr_var, expr)));

        size_t len = init->multi->len;
        size_t prev_index = 0, index = 0;
        for (size_t i = 0; i < len; ++i) {
          Initializer *init_elem = init->multi->data[i];
          if (init_elem->kind == IK_ARR) {
            Expr *ind = init_elem->arr.index;
            if (ind->kind != EX_FIXNUM)
              parse_error(init_elem->token, "Number required");
            index = ind->fixnum;
            init_elem = init_elem->arr.value;
          }

          size_t add = index - prev_index;
          if (add > 0) {
            Fixnum n = add;
            vec_push(inits, new_stmt_expr(
                new_expr_unary(EX_MODIFY, ptr_type, NULL,
                               new_expr_bop(EX_PTRADD, ptr_type, NULL, ptr_var,
                                            new_expr_fixlit(&tySize, NULL, n)))));
          }

          assign_initial_value(new_expr_deref(NULL, ptr_var), init_elem, inits);
          prev_index = index++;
        }
      }
      break;
    case IK_SINGLE:
      // Special handling for string (char[]).
      if (is_char_type(expr->type->pa.ptrof) &&
          init->single->kind == EX_STR) {
        vec_push(inits, init_char_array_by_string(expr, init));
        break;
      }
      // Fallthrough
    default:
      parse_error(init->token, "Error initializer");
      break;
    }
    break;
  case TY_STRUCT:
    {
      if (init->kind != IK_MULTI) {
        vec_push(inits,
                 new_stmt_expr(new_expr_bop(EX_ASSIGN, expr->type, init->token, expr,
                                            init->single)));
        break;
      }

      const StructInfo *sinfo = expr->type->struct_.info;
      if (!sinfo->is_union) {
        for (int i = 0, n = sinfo->members->len; i < n; ++i) {
          const VarInfo* member = sinfo->members->data[i];
          Expr *mem = new_expr_member(NULL, member->type, expr, NULL, i);
          Initializer *init_elem = init->multi->data[i];
          if (init_elem != NULL)
            assign_initial_value(mem, init_elem, inits);
        }
      } else {
        int n = sinfo->members->len;
        int m = init->multi->len;
        if (n <= 0 && m > 0)
          parse_error(init->token, "Initializer for empty union");
        if (org_init->multi->len > 1)
          parse_error(init->token, "More than one initializer for union");

        for (int i = 0; i < n; ++i) {
          Initializer *init_elem = init->multi->data[i];
          if (init_elem == NULL)
            continue;
          const VarInfo* member = sinfo->members->data[i];
          Expr *mem = new_expr_member(NULL, member->type, expr, NULL, i);
          assign_initial_value(mem, init_elem, inits);
          break;
        }
      }
    }
    break;
  default:
    switch (init->kind) {
    case IK_MULTI:
      if (init->multi->len != 1 || ((Initializer*)init->multi->data[0])->kind != IK_SINGLE) {
        parse_error(init->token, "Error initializer");
        break;
      }
      init = init->multi->data[0];
      // Fallthrough
    case IK_SINGLE:
      {
        Expr *value = str_to_char_array_var(init->single);
        vec_push(inits,
                 new_stmt_expr(new_expr_bop(EX_ASSIGN, expr->type, init->token, expr,
                                            make_cast(expr->type, init->token, value, false))));
      }
      break;
    default:
      parse_error(init->token, "Error initializer");
      break;
    }
    break;
  }

  return inits;
}

Vector *construct_initializing_stmts(Vector *decls) {
  Vector *inits = NULL;
  for (int i = 0; i < decls->len; ++i) {
    VarDecl *decl = decls->data[i];
    if (decl->storage & VS_STATIC)
      continue;
    Expr *var = new_expr_variable(decl->ident->ident, decl->type, NULL, curscope);
    inits = assign_initial_value(var, decl->init, inits);
  }
  return inits;
}

static Initializer *check_vardecl(const Type *type, const Token *ident, int storage, Initializer *init) {
  if (type->kind == TY_ARRAY && init != NULL)
    fix_array_size((Type*)type, init);
  if (type->kind == TY_STRUCT)
    ensure_struct((Type*)type, NULL, curscope);

  if (curfunc != NULL) {
    VarInfo *varinfo = scope_find(curscope, ident->ident, NULL);

    // TODO: Check `init` can be cast to `type`.
    if (storage & VS_STATIC) {
      VarInfo *gvarinfo = varinfo->static_.gvar;
      assert(gvarinfo != NULL);
      gvarinfo->global.init = init = check_global_initializer(type, init);
      // static variable initializer is handled in codegen, same as global variable.
    }
  } else {
    //intptr_t eval;
    //if (find_enum_value(ident->ident, &eval))
    //  parse_error(ident, "`%.*s' is already defined", ident->ident->bytes, ident->ident->chars);
    if (storage & VS_EXTERN && init != NULL)
      parse_error(init->token, "extern with initializer");
    // Toplevel
    VarInfo *varinfo = scope_find(global_scope, ident->ident, NULL);
    assert(varinfo != NULL);
    varinfo->global.init = init = check_global_initializer(type, init);
  }
  return init;
}

static void add_func_label(const Token *label) {
  assert(curfunc != NULL);
  Table *table = curfunc->label_table;
  if (table == NULL) {
    curfunc->label_table = table = malloc(sizeof(*table));
    table_init(table);
  }
  if (!table_put(table, label->ident, (void*)-1))  // Put dummy value.
    parse_error(label, "Label `%.*s' already defined", label->ident->bytes, label->ident->chars);
}

static void add_func_goto(Stmt *stmt) {
  assert(curfunc != NULL);
  if (curfunc->gotos == NULL)
    curfunc->gotos = new_vector();
  vec_push(curfunc->gotos, stmt);
}

// Scope

static Scope *enter_scope(Function *func, Vector *vars) {
  Scope *scope = new_scope(curscope, vars);
  curscope = scope;
  vec_push(func->scopes, scope);
  return scope;
}

static void exit_scope(void) {
  assert(!is_global_scope(curscope));
  curscope = curscope->parent;
}

static Vector *parse_vardecl_cont(const Type *rawType, Type *type, int storage, Token *ident) {
  Vector *decls = NULL;
  bool first = true;
  do {
    if (!first) {
      if (!parse_var_def(&rawType, (const Type**)&type, &storage, &ident) || ident == NULL) {
        parse_error(NULL, "`ident' expected");
        return NULL;
      }
    }
    first = false;

    Initializer *init = NULL;
    if (match(TK_LPAR)) {  // Function prototype.
      bool vaargs;
      Vector *params = parse_funparams(&vaargs);
      Vector *param_types = extract_varinfo_types(params);
      type = new_func_type(type, params, param_types, vaargs);
      storage |= VS_EXTERN;
    } else {
      not_void(type, NULL);

      assert(!is_global_scope(curscope));
      scope_add(curscope, ident, type, storage);

      if (match(TK_ASSIGN)) {
        init = parse_initializer();
      }
    }

    init = check_vardecl(type, ident, storage, init);
    VarDecl *decl = new_vardecl(type, ident, init, storage);
    if (decls == NULL)
      decls = new_vector();
    vec_push(decls, decl);
  } while (match(TK_COMMA));
  return decls;
}

static Stmt *parse_vardecl(void) {
  const Type *rawType = NULL;
  Type *type;
  int storage;
  Token *ident;
  if (!parse_var_def(&rawType, (const Type**)&type, &storage, &ident))
    return NULL;
  if (ident == NULL) {
    if ((type->kind == TY_STRUCT ||
         (type->kind == TY_FIXNUM && type->fixnum.kind == FX_ENUM)) &&
         match(TK_SEMICOL)) {
      // Just struct/union or enum definition.
    } else {
      parse_error(NULL, "Ident expected");
    }
    return new_stmt_block(NULL, NULL, NULL);  // Empty statement.
  }

  Vector *decls = parse_vardecl_cont(rawType, type, storage, ident);

  consume(TK_SEMICOL, "`;' expected");

  if (decls == NULL)
    return NULL;
  Vector *inits = !is_global_scope(curscope) ? construct_initializing_stmts(decls) : NULL;
  return new_stmt_vardecl(decls, inits);
}

static void parse_typedef(void) {
  int storage;
  Token *ident;
  const Type *type = parse_full_type(&storage, &ident);
  if (type == NULL)
    parse_error(NULL, "type expected");
  not_void(type, NULL);

  if (ident == NULL) {
    ident = consume(TK_IDENT, "ident expected");
  }
  const Name *name = ident->ident;
  Scope *scope;
  const Type *conflict = find_typedef(curscope, name, &scope);
  if (conflict != NULL && scope == curscope) {
    if (!same_type(type, conflict))
      parse_error(ident, "Conflict typedef");
  } else {
    conflict = NULL;
  }

  if (conflict == NULL || (type->kind == TY_STRUCT && type->struct_.info != NULL))
    add_typedef(curscope, name, type);

  consume(TK_SEMICOL, "`;' expected");
}

static Stmt *parse_if(const Token *tok) {
  consume(TK_LPAR, "`(' expected");
  Expr *cond = make_cond(parse_expr());
  consume(TK_RPAR, "`)' expected");
  Stmt *tblock = parse_stmt();
  Stmt *fblock = NULL;
  if (match(TK_ELSE)) {
    fblock = parse_stmt();
  }
  return new_stmt_if(tok, cond, tblock, fblock);
}

static Stmt *parse_switch(const Token *tok) {
  consume(TK_LPAR, "`(' expected");
  Expr *value = parse_expr();
  not_void(value->type, value->token);
  consume(TK_RPAR, "`)' expected");

  Stmt *swtch = new_stmt_switch(tok, value);
  Stmt *save_switch = curswitch;
  int save_flag = curloopflag;
  curloopflag |= LF_BREAK;
  curswitch = swtch;

  swtch->switch_.body = parse_stmt();

  curloopflag = save_flag;
  curswitch = save_switch;

  return swtch;
}

static Stmt *parse_case(const Token *tok) {
  Expr *value = parse_const();
  consume(TK_COLON, "`:' expected");

  if (curswitch == NULL)
    parse_error(tok, "`case' cannot use outside of `switch`");

  if (!is_fixnum(value->type->kind))
    parse_error(value->token, "Cannot use expression");
  intptr_t v = value->fixnum;

  // Check duplication.
  Vector *values = curswitch->switch_.case_values;
  for (int i = 0, len = values->len; i < len; ++i) {
    if ((intptr_t)values->data[i] == v)
      parse_error(tok, "Case value `%" PRIdPTR "' already defined", v);
  }
  vec_push(values, (void*)v);

  return new_stmt_case(tok, value);
}

static Stmt *parse_default(const Token *tok) {
  consume(TK_COLON, "`:' expected");

  if (curswitch == NULL)
    parse_error(tok, "`default' cannot use outside of `switch'");
  if (curswitch->switch_.has_default)
    parse_error(tok, "`default' already defined in `switch'");

  curswitch->switch_.has_default = true;

  return new_stmt_default(tok);
}

static Stmt *parse_while(const Token *tok) {
  consume(TK_LPAR, "`(' expected");
  Expr *cond = make_cond(parse_expr());
  consume(TK_RPAR, "`)' expected");

  int save_flag = curloopflag;
  curloopflag |= LF_BREAK | LF_CONTINUE;

  Stmt *body = parse_stmt();

  curloopflag = save_flag;

  return new_stmt_while(tok, cond, body);
}

static Stmt *parse_do_while(void) {
  int save_flag = curloopflag;
  curloopflag |= LF_BREAK | LF_CONTINUE;

  Stmt *body = parse_stmt();

  curloopflag = save_flag;

  const Token *tok = consume(TK_WHILE, "`while' expected");
  consume(TK_LPAR, "`(' expected");
  Expr *cond = make_cond(parse_expr());
  consume(TK_RPAR, "`)' expected");
  consume(TK_SEMICOL, "`;' expected");
  return new_stmt_do_while(body, tok, cond);
}

static Stmt *parse_for(const Token *tok) {
  consume(TK_LPAR, "`(' expected");
  Expr *pre = NULL;
  Vector *decls = NULL;
  Scope *scope = NULL;
  if (!match(TK_SEMICOL)) {
    const Type *rawType = NULL;
    Type *type;
    int storage;
    Token *ident;
    if (parse_var_def(&rawType, (const Type**)&type, &storage, &ident)) {
      if (ident == NULL)
        parse_error(NULL, "Ident expected");
      scope = enter_scope(curfunc, NULL);
      decls = parse_vardecl_cont(rawType, type, storage, ident);
      consume(TK_SEMICOL, "`;' expected");
    } else {
      pre = parse_expr();
      consume(TK_SEMICOL, "`;' expected");
    }
  }

  Expr *cond = NULL;
  Expr *post = NULL;
  Stmt *body = NULL;
  if (!match(TK_SEMICOL)) {
    cond = make_cond(parse_expr());
    consume(TK_SEMICOL, "`;' expected");
  }
  if (!match(TK_RPAR)) {
    post = parse_expr();
    consume(TK_RPAR, "`)' expected");
  }

  int save_flag = curloopflag;
  curloopflag |= LF_BREAK | LF_CONTINUE;

  body = parse_stmt();

  Vector *stmts = new_vector();
  if (decls != NULL) {
    Vector *inits = construct_initializing_stmts(decls);
    vec_push(stmts, new_stmt_vardecl(decls, inits));
  }

  curloopflag = save_flag;

  if (scope != NULL)
    exit_scope();

  Stmt *stmt = new_stmt_for(tok, pre, cond, post, body);
  vec_push(stmts, stmt);
  return new_stmt_block(tok, stmts, scope);
}

static Stmt *parse_break_continue(enum StmtKind kind, const Token *tok) {
  consume(TK_SEMICOL, "`;' expected");
  if ((curloopflag & LF_BREAK) == 0) {
    const char *err;
    if (kind == ST_BREAK)
      err = "`break' cannot be used outside of loop";
    else
      err = "`continue' cannot be used outside of loop";
    parse_error(tok, err);
  }
  return new_stmt(kind, tok);
}

static Stmt *parse_goto(void) {
  Token *label = consume(TK_IDENT, "label for goto expected");
  consume(TK_SEMICOL, "`;' expected");

  Stmt *stmt = new_stmt_goto(label);
  add_func_goto(stmt);
  return stmt;
}

static Stmt *parse_label(const Token *label) {
  Stmt *stmt = new_stmt_label(label, parse_stmt());
  add_func_label(label);
  return stmt;
}

static Stmt *parse_return(const Token *tok) {
  Expr *val = NULL;
  if (!match(TK_SEMICOL)) {
    val = parse_expr();
    consume(TK_SEMICOL, "`;' expected");
    val = str_to_char_array_var(val);
  }

  assert(curfunc != NULL);
  const Type *rettype = curfunc->type->func.ret;
  if (val == NULL) {
    if (rettype->kind != TY_VOID)
      parse_error(tok, "`return' required a value");
  } else {
    if (rettype->kind == TY_VOID)
      parse_error(val->token, "void function `return' a value");

    val = make_cast(rettype, val->token, val, false);
  }

  return new_stmt_return(tok, val);
}

static Stmt *parse_asm(const Token *tok) {
  consume(TK_LPAR, "`(' expected");

  Token *token;
  Vector *args = parse_args(&token);

  if (args == NULL || args->len != 1 || ((Expr*)args->data[0])->kind != EX_STR)
    parse_error(token, "`__asm' expected one string");

  return new_stmt_asm(tok, args->data[0]);
}

// Multiple stmt-s, also accept `case` and `default`.
static Vector *parse_stmts(void) {
  Vector *stmts = new_vector();
  for (;;) {
    if (match(TK_RBRACE))
      return stmts;

    Stmt *stmt;
    Token *tok;
    if ((stmt = parse_vardecl()) != NULL)
      ;
    else if ((tok = match(TK_CASE)) != NULL)
      stmt = parse_case(tok);
    else if ((tok = match(TK_DEFAULT)) != NULL)
      stmt = parse_default(tok);
    else
      stmt = parse_stmt();

    if (stmt == NULL)
      continue;
    vec_push(stmts, stmt);
  }
}

static Stmt *parse_block(const Token *tok) {
  Scope *scope = enter_scope(curfunc, NULL);
  Vector *stmts = parse_stmts();
  Stmt *stmt = new_stmt_block(tok, stmts, scope);
  exit_scope();
  return stmt;
}

static Stmt *parse_stmt(void) {
  Token *label = match(TK_IDENT);
  if (label != NULL) {
    if (match(TK_COLON)) {
      return parse_label(label);
    }
    unget_token(label);
  }

  if (match(TK_SEMICOL))
    return NULL;

  const Token *tok;
  if ((tok = match(TK_LBRACE)) != NULL)
    return parse_block(tok);

  if ((tok = match(TK_IF)) != NULL)
    return parse_if(tok);

  if ((tok = match(TK_SWITCH)) != NULL)
    return parse_switch(tok);

  if ((tok = match(TK_WHILE)) != NULL)
    return parse_while(tok);

  if (match(TK_DO))
    return parse_do_while();

  if ((tok = match(TK_FOR)) != NULL)
    return parse_for(tok);

  if ((tok = match(TK_BREAK)) != NULL) {
    return parse_break_continue(ST_BREAK, tok);
  }
  if ((tok = match(TK_CONTINUE)) != NULL) {
    return parse_break_continue(ST_CONTINUE, tok);
  }
  if (match(TK_GOTO)) {
    return parse_goto();
  }

  if ((tok = match(TK_RETURN)) != NULL)
    return parse_return(tok);

  if ((tok = match(TK_TYPEDEF)) != NULL) {
    parse_typedef();
    return NULL;
  }

  if ((tok = match(TK_ASM)) != NULL)
    return parse_asm(tok);

  // expression statement.
  Expr *val = parse_expr();
  consume(TK_SEMICOL, "`;' expected");
  return new_stmt_expr(str_to_char_array_var(val));
}

static Declaration *parse_defun(const Type *functype, int storage, Token *ident) {
  assert(functype->kind == TY_FUNC);
  Function *func = new_func(functype, ident->ident);

  VarInfo *varinfo = scope_find(global_scope, func->name, NULL);
  if (varinfo == NULL) {
    varinfo = scope_add(global_scope, ident, functype, storage);
  } else {
    if (varinfo->type->kind != TY_FUNC)
      parse_error(ident, "Definition conflict: `%s'");
    // TODO: Check type.
    // TODO: Check duplicated definition.
    if (varinfo->global.init != NULL)
      parse_error(ident, "`%.*s' function already defined", func->name->bytes,
                  func->name->chars);
  }

  if (match(TK_SEMICOL)) {
    // Prototype declaration.
  } else {
    consume(TK_LBRACE, "`;' or `{' expected");

    assert(curfunc == NULL);
    assert(is_global_scope(curscope));
    curfunc = func;
    Vector *top_vars = NULL;
    Vector *params = func->type->func.params;
    if (params != NULL) {
      top_vars = new_vector();
      for (int i = 0; i < params->len; ++i)
        vec_push(top_vars, params->data[i]);
    }
    func->scopes = new_vector();
    enter_scope(func, top_vars);  // Scope for parameters.
    func->stmts = parse_stmts();
    exit_scope();
    assert(is_global_scope(curscope));

    // Check goto labels.
    if (func->gotos != NULL) {
      Vector *gotos = func->gotos;
      Table *label_table = func->label_table;
      for (int i = 0; i < gotos->len; ++i) {
        Stmt *stmt = gotos->data[i];
        void *bb;
        if (label_table == NULL || !table_try_get(label_table, stmt->goto_.label->ident, &bb)) {
          const Name *name = stmt->goto_.label->ident;
          parse_error(stmt->goto_.label, "`%.*s' not found", name->bytes, name->chars);
        }
      }
    }

    curfunc = NULL;
  }
  return new_decl_defun(func);
}

static Declaration *parse_global_var_decl(
    const Type *rawtype, int storage, const Type *type, Token *ident
) {
  Vector *decls = NULL;
  for (;;) {
    if (type->kind == TY_VOID)
      parse_error(ident, "`void' not allowed");

    if (!(type->kind == TY_PTR && type->pa.ptrof->kind == TY_FUNC))
      type = parse_type_suffix(type);

    VarInfo *varinfo = scope_add(global_scope, ident, type, storage);

    Initializer *init = NULL;
    if (match(TK_ASSIGN) != NULL)
      init = parse_initializer();
    varinfo->global.init = init;

    init = check_vardecl(type, ident, storage, init);
    VarDecl *decl = new_vardecl(type, ident, init, storage);
    if (decls == NULL)
      decls = new_vector();
    vec_push(decls, decl);
    if (!match(TK_COMMA))
      break;

    // Next declaration.
    type = parse_type_modifier(rawtype);
    ident = consume(TK_IDENT, "`ident' expected");
  }

  consume(TK_SEMICOL, "`;' or `,' expected");

  if (decls == NULL)
    return NULL;
  return new_decl_vardecl(decls);
}

static Declaration *parse_declaration(void) {
  const Type *rawtype = NULL, *type;
  int storage;
  Token *ident;
  if (parse_var_def(&rawtype, &type, &storage, &ident)) {
    if (ident == NULL) {
      if ((type->kind == TY_STRUCT ||
           (type->kind == TY_FIXNUM && type->fixnum.kind == FX_ENUM)) &&
          match(TK_SEMICOL)) {
        // Just struct/union or enum definition.
      } else {
        parse_error(NULL, "Ident expected");
      }
      return NULL;
    }

    if (type->kind == TY_FUNC)
      return parse_defun(type, storage, ident);

    return parse_global_var_decl(rawtype, storage, type, ident);
  }
  if (match(TK_TYPEDEF)) {
    parse_typedef();
    return NULL;
  }
  parse_error(NULL, "Unexpected token");
  return NULL;
}

void parse(Vector *decls) {
  curscope = global_scope;

  while (!match(TK_EOF)) {
    Declaration *decl = parse_declaration();
    if (decl != NULL)
      vec_push(decls, decl);
  }
}
