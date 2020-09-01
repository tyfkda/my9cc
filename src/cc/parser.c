#include "parser.h"

#include <assert.h>
#include <inttypes.h>  // PRIdPTR
#include <stdbool.h>
#include <stdlib.h>  // malloc

#include "ast.h"
#include "lexer.h"
#include "table.h"
#include "type.h"
#include "util.h"
#include "var.h"

const int LF_BREAK = 1 << 0;
const int LF_CONTINUE = 1 << 0;

Defun *curdefun;
static int curloopflag;
static Stmt *curswitch;

static Stmt *parse_stmt(void);

void fix_array_size(Type *type, Initializer *init) {
  assert(init != NULL);
  assert(type->kind == TY_ARRAY);

  bool is_str = (is_char_type(type->pa.ptrof) &&
                 init->kind == IK_SINGLE &&
                 init->single->kind == EX_STR);
  if (!is_str && init->kind != IK_MULTI) {
    parse_error(init->token, "Error initializer");
    return;
  }

  size_t arr_len = type->pa.length;
  if (arr_len == (size_t)-1) {
    if (is_str) {
      type->pa.length = init->single->str.size;
    } else {
      size_t index = 0;
      size_t max_index = 0;
      size_t i, len = init->multi->len;
      for (i = 0; i < len; ++i) {
        Initializer *init_elem = init->multi->data[i];
        if (init_elem->kind == IK_ARR) {
          assert(init_elem->arr.index->kind == EX_NUM);
          index = init_elem->arr.index->num.ival;
        }
        ++index;
        if (max_index < index)
          max_index = index;
      }
      type->pa.length = max_index;
    }
  } else {
    assert(!is_str || init->single->kind == EX_STR);
    size_t init_len = is_str ? init->single->str.size : (size_t)init->multi->len;
    if (init_len > arr_len) {
      parse_error(NULL, "Initializer more than array size");
      return;
    }
  }
}

static Stmt *build_memcpy(Expr *dst, Expr *src, size_t size) {
  assert(curscope != NULL);
  const Type *charptr_type = ptrof(&tyChar);
  VarInfo *dstvar = add_cur_scope(alloc_ident(alloc_label(), NULL, NULL), charptr_type, 0);
  VarInfo *srcvar = add_cur_scope(alloc_ident(alloc_label(), NULL, NULL), charptr_type, 0);
  VarInfo *sizevar = add_cur_scope(alloc_ident(alloc_label(), NULL, NULL), &tySize, 0);
  Expr *dstexpr = new_expr_variable(dstvar->name, dstvar->type, NULL, curscope);
  Expr *srcexpr = new_expr_variable(srcvar->name, srcvar->type, NULL, curscope);
  Expr *sizeexpr = new_expr_variable(sizevar->name, sizevar->type, NULL, curscope);

  Num size_num_lit = {.ival = size};
  Expr *size_num = new_expr_numlit(&tySize, NULL, &size_num_lit);

  Num zero = {.ival = 0};
  Expr *zeroexpr = new_expr_numlit(&tySize, NULL, &zero);

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

// Convert string literal to global char-array variable reference.
Initializer *convert_str_to_ptr_initializer(const Type *type, Initializer *init) {
  assert(type->kind == TY_ARRAY && is_char_type(type->pa.ptrof));
  VarInfo *varinfo = str_to_char_array(type, init);
  Initializer *init2 = malloc(sizeof(*init2));
  init2->kind = IK_SINGLE;
  init2->single = new_expr_variable(varinfo->name, type, NULL, NULL);
  init2->token = init->token;
  return init2;
}

static Stmt *init_char_array_by_string(Expr *dst, Initializer *src) {
  // Initialize char[] with string literal (char s[] = "foo";).
  assert(src->kind == IK_SINGLE);
  const Expr *str = src->single;
  assert(str->kind == EX_STR);
  assert(dst->type->kind == TY_ARRAY && is_char_type(dst->type->pa.ptrof));

  size_t size = str->str.size;
  size_t dstsize = dst->type->pa.length;
  if (dstsize == (size_t)-1) {
    ((Type*)dst->type)->pa.length = dstsize = size;
  } else {
    if (dstsize < size) {
      parse_error(NULL, "Buffer is shorter than string: %d for \"%s\"", (int)dstsize, str);
      return NULL;
    }
  }

  const Type *strtype = dst->type;
  VarInfo *varinfo = str_to_char_array(strtype, src);
  Expr *var = new_expr_variable(varinfo->name, strtype, NULL, NULL);
  return build_memcpy(dst, var, size);
}

static int compare_desig_start(const void *a, const void *b) {
  const size_t *pa = *(size_t**)a;
  const size_t *pb = *(size_t**)b;
  intptr_t d = *pa - *pb;
  return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static Initializer *flatten_array_initializer(Initializer *init) {
  // Check whether IK_DOT or IK_ARR exists.
  int i = 0, len = init->multi->len;
  for (; i < len; ++i) {
    Initializer *init_elem = init->multi->data[i];
    if (init_elem->kind == IK_DOT) {
      parse_error(NULL, "dot initializer for array");
      return init;
    }
    if (init_elem->kind == IK_ARR)
      break;
  }
  if (i >= len)  // IK_ARR not exits.
    return init;

  // Enumerate designated initializer.
  Vector *ranges = new_vector();  // <(start, count)>
  size_t lastStartIndex = 0;
  size_t lastStart = 0;
  size_t index = i;
  for (; i <= len; ++i, ++index) {  // '+1' is for last range.
    Initializer *init_elem = NULL;
    if (i >= len || (init_elem = init->multi->data[i])->kind == IK_ARR) {
      if (i < len && init_elem->arr.index->kind != EX_NUM) {
        parse_error(NULL, "Constant value expected");
        return init;
      }
      if ((size_t)i > lastStartIndex) {
        size_t *range = malloc(sizeof(size_t) * 3);
        range[0] = lastStart;
        range[1] = lastStartIndex;
        range[2] = index - lastStart;
        vec_push(ranges, range);
      }
      if (i >= len)
        break;
      lastStart = index = init_elem->arr.index->num.ival;
      lastStartIndex = i;
    } else if (init_elem->kind == IK_DOT) {
      parse_error(NULL, "dot initializer for array");
      return init;
    }
  }

  // Sort
  myqsort(ranges->data, ranges->len, sizeof(size_t*), compare_desig_start);

  // Reorder
  Vector *reordered = new_vector();
  index = 0;
  for (int i = 0; i < ranges->len; ++i) {
    size_t *p = ranges->data[i];
    size_t start = p[0];
    size_t index = p[1];
    size_t count = p[2];
    if (i > 0) {
      size_t *q = ranges->data[i - 1];
      if (start < q[0] + q[2]) {
        parse_error(NULL, "Initializer for array overlapped");
        return init;
      }
    }
    for (size_t j = 0; j < count; ++j) {
      Initializer *elem = init->multi->data[index + j];
      if (j == 0 && index != start && elem->kind != IK_ARR) {
        Initializer *arr = malloc(sizeof(*arr));
        arr->kind = IK_ARR;
        Num n = {.ival = start};
        arr->arr.index = new_expr_numlit(&tyInt, NULL, &n);
        arr->arr.value = elem;
        elem = arr;
      }
      vec_push(reordered, elem);
    }
  }

  Initializer *init2 = malloc(sizeof(*init2));
  init2->kind = IK_MULTI;
  init2->multi = reordered;
  return init2;
}

static Initializer *flatten_initializer(const Type *type, Initializer *init) {
  if (init == NULL)
    return NULL;

  switch (type->kind) {
  case TY_STRUCT:
    if (init->kind == IK_MULTI) {
      ensure_struct((Type*)type, NULL);
      const StructInfo *sinfo = type->struct_.info;
      int n = sinfo->members->len;
      int m = init->multi->len;
      if (n <= 0) {
        if (m > 0) {
          parse_error(init->token, "Initializer for empty struct");
          return init;
        }
        return NULL;
      }
      if (sinfo->is_union && m > 1) {
        parse_error(((Initializer*)init->multi->data[1])->token, "Initializer for union more than 1");
        return init;
      }

      Initializer **values = malloc(sizeof(Initializer*) * n);
      for (int i = 0; i < n; ++i)
        values[i] = NULL;

      int index = 0;
      for (int i = 0; i < m; ++i) {
        Initializer *value = init->multi->data[i];
        if (value->kind == IK_ARR) {
          parse_error(NULL, "indexed initializer for struct");
          return init;
        }

        if (value->kind == IK_DOT) {
          const Name *name = value->dot.name;
          index = var_find(sinfo->members, name);
          if (index >= 0) {
            value = value->dot.value;
          } else {
            Vector *stack = new_vector();
            if (search_from_anonymous(type, name, NULL, stack) == NULL) {
              parse_error(value->token, "`%.*s' is not member of struct", name->bytes, name->chars);
              return init;
            }

            index = (intptr_t)stack->data[0];
            Vector *multi = new_vector();
            vec_push(multi, value);
            Initializer *init2 = malloc(sizeof(*init2));
            init2->kind = IK_MULTI;
            init2->multi = multi;
            value = init2;
          }
        }
        if (index >= n) {
          parse_error(NULL, "Too many init values");
          return init;
        }

        // Allocate string literal for char* as a char array.
        if (value->kind == IK_SINGLE && value->single->kind == EX_STR) {
          const VarInfo *member = sinfo->members->data[index];
          if (member->type->kind == TY_PTR &&
              is_char_type(member->type->pa.ptrof)) {
            value = convert_str_to_ptr_initializer(value->single->type, value);
          }
        }

        values[index++] = value;
      }

      Initializer *flat = malloc(sizeof(*flat));
      flat->kind = IK_MULTI;
      Vector *v = malloc(sizeof(*v));
      v->len = v->capacity = n;
      v->data = (void**)values;
      flat->multi = v;

      return flat;
    }
    break;
  case TY_ARRAY:
    switch (init->kind) {
    case IK_MULTI:
      init = flatten_array_initializer(init);
      break;
    case IK_SINGLE:
      // Special handling for string (char[]), and accept length difference.
      if (init->single->type->kind == TY_ARRAY &&
          can_cast(type->pa.ptrof, init->single->type->pa.ptrof, is_zero(init->single), false))
        break;
      // Fallthrough
    default:
      parse_error(NULL, "Illegal initializer");
      break;
    }
  default:
    break;
  }
  return init;
}

static Initializer *check_global_initializer(const Type *type, Initializer *init) {
  if (init == NULL)
    return NULL;

  init = flatten_initializer(type, init);

  switch (type->kind) {
  case TY_NUM:
    if (init->kind == IK_SINGLE) {
      switch (init->single->kind) {
      case EX_NUM:
        return init;
      default:
        parse_error(init->single->token, "Constant expression expected");
        break;
      }
    }
    break;
  case TY_PTR:
    {
      if (init->kind != IK_SINGLE) {
        parse_error(NULL, "initializer type error");
        break;
      }

      Expr *value = init->single;
      while (value->kind == EX_CAST || value->kind == EX_GROUP) {
        value = value->unary.sub;
      }

      switch (value->kind) {
      case EX_REF:
        {
          value = value->unary.sub;
          if (value->kind != EX_VARIABLE) {
            parse_error(value->token, "pointer initializer must be variable");
            break;
          }
          const Name *name = value->variable.name;
          if (value->variable.scope != NULL) {
            Scope *scope = value->variable.scope;
            VarInfo *varinfo = scope_find(&scope, name);
            assert(varinfo != NULL);
            if (!(varinfo->flag & VF_STATIC)) {
              parse_error(value->token, "Allowed global reference only");
              break;
            }
            name = varinfo->local.label;
          }

          VarInfo *info = find_global(name);
          assert(info != NULL);

          if (!same_type(type->pa.ptrof, info->type)) {
            parse_error(value->token, "Illegal type");
            break;
          }

          return init;
        }
      case EX_VARIABLE:
        {
          const Name *name = value->variable.name;
          if (value->variable.scope != NULL) {
            Scope *scope = value->variable.scope;
            VarInfo *varinfo = scope_find(&scope, name);
            assert(varinfo != NULL);
            if (!(varinfo->flag & VF_STATIC)) {
              parse_error(value->token, "Allowed global reference only");
              break;
            }
            name = varinfo->local.label;
          }

          VarInfo *info = find_global(name);
          assert(info != NULL);

          if ((info->type->kind != TY_ARRAY && info->type->kind != TY_FUNC) ||
              !can_cast(type, info->type, is_zero(value), false)) {
            parse_error(value->token, "Illegal type");
            break;
          }

          return init;
        }
      case EX_NUM:
        {
          Initializer *init2 = malloc(sizeof(*init2));
          init2->kind = IK_SINGLE;
          init2->single = value;
          return init2;
        }
      case EX_STR:
        {
          if (!is_char_type(type->pa.ptrof)) {
            parse_error(value->token, "Illegal type");
            break;
          }

          // Create string and point to it.
          Type* strtype = arrayof(type->pa.ptrof, value->str.size);
          return convert_str_to_ptr_initializer(strtype, init);
        }
      default:
        break;
      }
      parse_error(value->token, "initializer type error: kind=%d", value->kind);
    }
    break;
  case TY_ARRAY:
    switch (init->kind) {
    case IK_MULTI:
      {
        const Type *elemtype = type->pa.ptrof;
        Vector *multi = init->multi;
        for (int i = 0, len = multi->len; i < len; ++i) {
          Initializer *eleminit = multi->data[i];
          multi->data[i] = check_global_initializer(elemtype, eleminit);
        }
      }
      break;
    case IK_SINGLE:
      if (is_char_type(type->pa.ptrof) && init->single->kind == EX_STR) {
        assert(type->pa.length != (size_t)-1);
        if (type->pa.length < init->single->str.size) {
          parse_error(init->single->token, "Array size shorter than initializer");
          break;
        }
        break;
      }
      // Fallthrough
    case IK_DOT:
    default:
      parse_error(NULL, "Illegal initializer");
      break;
    }
    break;
  case TY_STRUCT:
    {
      const StructInfo *sinfo = type->struct_.info;
      for (int i = 0, n = sinfo->members->len; i < n; ++i) {
        const VarInfo* member = sinfo->members->data[i];
        Initializer *init_elem = init->multi->data[i];
        if (init_elem != NULL)
          init->multi->data[i] = check_global_initializer(member->type, init_elem);
      }
    }
    break;
  default:
    parse_error(NULL, "Global initial value for type %d not implemented (yet)\n", type->kind);
    break;
  }
  return init;
}

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
        if ((size_t)init->multi->len > arr_len) {
          parse_error(NULL, "Initializer more than array size");
          break;
        }

        assert(curscope != NULL);
        const Type *ptr_type = array_to_ptr(expr->type);
        VarInfo *ptr_varinfo = add_cur_scope(alloc_ident(alloc_label(), NULL, NULL), ptr_type, 0);
        Expr *ptr_var = new_expr_variable(ptr_varinfo->name, ptr_type, NULL, curscope);
        vec_push(inits, new_stmt_expr(new_expr_bop(EX_ASSIGN, ptr_type, NULL, ptr_var, expr)));

        size_t len = init->multi->len;
        size_t prev_index = 0, index = 0;
        for (size_t i = 0; i < len; ++i) {
          Initializer *init_elem = init->multi->data[i];
          if (init_elem->kind == IK_ARR) {
            Expr *ind = init_elem->arr.index;
            if (ind->kind != EX_NUM) {
              parse_error(NULL, "Number required");
              break;
            }
            index = ind->num.ival;
            init_elem = init_elem->arr.value;
          }

          size_t add = index - prev_index;
          if (add > 0) {
            Num n = {.ival=add};
            vec_push(inits, new_stmt_expr(
                new_expr_unary(EX_ASSIGN_WITH, ptr_type, NULL,
                               new_expr_bop(EX_PTRADD, ptr_type, NULL, ptr_var,
                                            new_expr_numlit(&tyInt, NULL, &n)))));
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
                 new_stmt_expr(new_expr_bop(EX_ASSIGN, expr->type, NULL, expr,
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
        if (n <= 0 && m > 0) {
          parse_error(NULL, "Initializer for empty union");
          break;
        }
        if (org_init->multi->len > 1) {
          parse_error(NULL, "More than one initializer for union");
          break;
        }

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
      vec_push(inits,
               new_stmt_expr(new_expr_bop(EX_ASSIGN, expr->type, NULL, expr,
                                          make_cast(expr->type, NULL, init->single, false))));
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
    if (decl->flag & VF_STATIC)
      continue;
    Expr *var = new_expr_variable(decl->ident->ident, decl->type, NULL, curscope);
    inits = assign_initial_value(var, decl->init, inits);
  }
  return inits;
}

static Initializer *check_vardecl(const Type *type, const Token *ident, int flag, Initializer *init) {
  if (type->kind == TY_ARRAY && init != NULL)
    fix_array_size((Type*)type, init);
  if (type->kind == TY_STRUCT)
    ensure_struct((Type*)type, NULL);

  if (curdefun != NULL) {
    Scope *scope = curscope;
    VarInfo *varinfo = scope_find(&scope, ident->ident);

    // TODO: Check `init` can be cast to `type`.
    if (flag & VF_STATIC) {
      VarInfo *gvarinfo = find_global(varinfo->local.label);
      assert(gvarinfo != NULL);
      gvarinfo->global.init = init = check_global_initializer(type, init);
      // static variable initializer is handled in codegen, same as global variable.
    }
  } else {
    //intptr_t eval;
    //if (find_enum_value(ident->ident, &eval))
    //  parse_error(ident, "`%.*s' is already defined", ident->ident->bytes, ident->ident->chars);
    if (flag & VF_EXTERN && init != NULL) {
      parse_error(init->token, "extern with initializer");
      return init;
    }
    // Toplevel
    VarInfo *varinfo = find_global(ident->ident);
    assert(varinfo != NULL);
    varinfo->global.init = init = check_global_initializer(type, init);
  }
  return init;
}

static void add_func_label(const Token *label) {
  assert(curdefun != NULL);
  if (curdefun->label_table == NULL) {
    curdefun->label_table = malloc(sizeof(*curdefun->label_table));
    table_init(curdefun->label_table);
  }
  BB *bb;
  if (table_try_get(curdefun->label_table, label->ident, (void**)&bb)) {
    parse_error(label, "Label `%.*s' already defined", label->ident->bytes, label->ident->chars);
    return;
  }
  table_put(curdefun->label_table, label->ident, (void*)-1);  // Put dummy value.
}

static void add_func_goto(Stmt *stmt) {
  assert(curdefun != NULL);
  if (curdefun->gotos == NULL)
    curdefun->gotos = new_vector();
  vec_push(curdefun->gotos, stmt);
}

// Scope

static Scope *enter_scope(Defun *defun, Vector *vars) {
  Scope *scope = new_scope(curscope, vars);
  curscope = scope;
  vec_push(defun->func->scopes, scope);
  return scope;
}

static void exit_scope(void) {
  assert(curscope != NULL);
  curscope = curscope->parent;
}

// Initializer

Initializer *parse_initializer(void) {
  Initializer *result = malloc(sizeof(*result));
  const Token *lblace_tok;
  if ((lblace_tok = match(TK_LBRACE)) != NULL) {
    Vector *multi = new_vector();
    if (!match(TK_RBRACE)) {
      for (;;) {
        Initializer *init;
        const Token *tok;
        if (match(TK_DOT)) {  // .member=value
          Token *ident = consume(TK_IDENT, "`ident' expected for dotted initializer");
          consume(TK_ASSIGN, "`=' expected for dotted initializer");
          Initializer *value = parse_initializer();
          init = malloc(sizeof(*init));
          init->kind = IK_DOT;
          init->token = ident;
          init->dot.name = ident->ident;
          init->dot.value = value;
        } else if ((tok = match(TK_LBRACKET)) != NULL) {
          Expr *index = parse_const();
          consume(TK_RBRACKET, "`]' expected");
          match(TK_ASSIGN);  // both accepted: `[1] = 2`, and `[1] 2`
          Initializer *value = parse_initializer();
          init = malloc(sizeof(*init));
          init->kind = IK_ARR;
          init->token = tok;
          init->arr.index = index;
          init->arr.value = value;
        } else {
          init = parse_initializer();
        }
        vec_push(multi, init);

        if (match(TK_COMMA)) {
          if (match(TK_RBRACE))
            break;
        } else {
          consume(TK_RBRACE, "`}' or `,' expected");
          break;
        }
      }
    }
    result->kind = IK_MULTI;
    result->token = lblace_tok;
    result->multi = multi;
  } else {
    result->kind = IK_SINGLE;
    result->single = parse_assign();
    result->token = result->single->token;
  }
  return result;
}

static Vector *parse_vardecl_cont(const Type *rawType, Type *type, int flag, Token *ident) {
  Vector *decls = NULL;
  bool first = true;
  do {
    if (!first) {
      if (!parse_var_def(&rawType, (const Type**)&type, &flag, &ident) || ident == NULL) {
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
      flag |= VF_EXTERN;
    } else {
      not_void(type);

      assert(curscope != NULL);
      add_cur_scope(ident, type, flag);

      if (match(TK_ASSIGN)) {
        init = parse_initializer();
      }
    }

    init = check_vardecl(type, ident, flag, init);
    VarDecl *decl = new_vardecl(type, ident, init, flag);
    if (decls == NULL)
      decls = new_vector();
    vec_push(decls, decl);
  } while (match(TK_COMMA));
  return decls;
}

static Stmt *parse_vardecl(void) {
  const Type *rawType = NULL;
  Type *type;
  int flag;
  Token *ident;
  if (!parse_var_def(&rawType, (const Type**)&type, &flag, &ident))
    return NULL;
  if (ident == NULL) {
    parse_error(NULL, "Ident expected");
    return NULL;
  }

  Vector *decls = parse_vardecl_cont(rawType, type, flag, ident);

  consume(TK_SEMICOL, "`;' expected");

  if (decls == NULL)
    return NULL;
  Vector *inits = curscope != NULL ? construct_initializing_stmts(decls) : NULL;
  return new_stmt_vardecl(decls, inits);
}

static Stmt *parse_if(const Token *tok) {
  consume(TK_LPAR, "`(' expected");
  Expr *cond = parse_expr();
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

  if (curswitch == NULL) {
    parse_error(tok, "`case' cannot use outside of `switch`");
    return NULL;
  }

  if (!is_number(value->type->kind)) {
    parse_error(value->token, "Cannot use expression");
    return NULL;
  }
  intptr_t v = value->num.ival;

  // Check duplication.
  Vector *values = curswitch->switch_.case_values;
  for (int i = 0, len = values->len; i < len; ++i) {
    if ((intptr_t)values->data[i] == v)
      parse_error(tok, "Case value `%"PRIdPTR"' already defined", v);
  }
  vec_push(values, (void*)v);

  return new_stmt_case(tok, value);
}

static Stmt *parse_default(const Token *tok) {
  consume(TK_COLON, "`:' expected");

  if (curswitch == NULL) {
    parse_error(tok, "`default' cannot use outside of `switch'");
    return NULL;
  }
  if (curswitch->switch_.has_default) {
    parse_error(tok, "`default' already defined in `switch'");
    return NULL;
  }

  curswitch->switch_.has_default = true;

  return new_stmt_default(tok);
}

static Stmt *parse_while(const Token *tok) {
  consume(TK_LPAR, "`(' expected");
  Expr *cond = parse_expr();
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
  Expr *cond = parse_expr();
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
    int flag;
    Token *ident;
    if (parse_var_def(&rawType, (const Type**)&type, &flag, &ident)) {
      if (ident == NULL) {
        parse_error(NULL, "Ident expected");
        return NULL;
      }
      scope = enter_scope(curdefun, NULL);
      decls = parse_vardecl_cont(rawType, type, flag, ident);
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
    cond = parse_expr();
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
    return NULL;
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
  }

  assert(curdefun != NULL);
  const Type *rettype = curdefun->func->type->func.ret;
  if (val == NULL) {
    if (rettype->kind != TY_VOID) {
      parse_error(tok, "`return' required a value");
      return NULL;
    }
  } else {
    if (rettype->kind == TY_VOID) {
      parse_error(val->token, "void function `return' a value");
      return NULL;
    }
    val = make_cast(rettype, val->token, val, false);
  }

  return new_stmt_return(tok, val);
}

static Stmt *parse_asm(const Token *tok) {
  consume(TK_LPAR, "`(' expected");

  Token *token;
  Vector *args = parse_args(&token);

  if (args == NULL || args->len != 1 || ((Expr*)args->data[0])->kind != EX_STR) {
    parse_error(token, "`__asm' expected one string");
    return NULL;
  }

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
  Scope *scope = enter_scope(curdefun, NULL);
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

  if ((tok = match(TK_ASM)) != NULL)
    return parse_asm(tok);

  // expression statement.
  Expr *val = parse_expr();
  consume(TK_SEMICOL, "`;' expected");
  return new_stmt_expr(val);
}

static Declaration *parse_defun(const Type *functype, int flag, Token *ident) {
  assert(functype->kind == TY_FUNC);
  Function *func = new_func(functype, ident->ident);
  Defun *defun = new_defun(func, flag);

  VarInfo *varinfo = find_global(defun->func->name);
  if (varinfo == NULL) {
    varinfo = define_global(functype, flag | VF_CONST, ident, ident->ident);
  } else {
    if (varinfo->type->kind != TY_FUNC) {
      parse_error(ident, "Definition conflict: `%s'");
      return NULL;
    }
    // TODO: Check type.
    // TODO: Check duplicated definition.
    if (varinfo->global.init != NULL) {
      parse_error(ident, "`%.*s' function already defined", defun->func->name->bytes,
                  defun->func->name->chars);
      return NULL;
    }
  }

  if (match(TK_SEMICOL)) {
    // Prototype declaration.
  } else {
    consume(TK_LBRACE, "`;' or `{' expected");

    assert(curdefun == NULL);
    assert(curscope == NULL);
    curdefun = defun;
    Vector *top_vars = NULL;
    Vector *params = defun->func->type->func.params;
    if (params != NULL) {
      top_vars = new_vector();
      for (int i = 0; i < params->len; ++i)
        vec_push(top_vars, params->data[i]);
    }
    defun->func->scopes = new_vector();
    enter_scope(defun, top_vars);  // Scope for parameters.
    defun->stmts = parse_stmts();
    exit_scope();
    assert(curscope == NULL);

    // Check goto labels.
    if (defun->gotos != NULL) {
      Vector *gotos = defun->gotos;
      Table *label_table = defun->label_table;
      for (int i = 0; i < gotos->len; ++i) {
        Stmt *stmt = gotos->data[i];
        void *bb;
        if (label_table == NULL || !table_try_get(label_table, stmt->goto_.label->ident, &bb)) {
          const Name *name = stmt->goto_.label->ident;
          parse_error(stmt->goto_.label, "`%.*s' not found", name->bytes, name->chars);
          return NULL;
        }
      }
    }

    curdefun = NULL;
  }
  return new_decl_defun(defun);
}

static void parse_typedef(void) {
  int flag;
  Token *ident;
  const Type *type = parse_full_type(&flag, &ident);
  if (type == NULL) {
    parse_error(NULL, "type expected");
    return;
  }
  not_void(type);

  if (ident == NULL) {
    ident = consume(TK_IDENT, "ident expected");
  }
  const Name *name = ident->ident;
  const Type *conflict = find_typedef(name);
  if (conflict != NULL) {
    if (!same_type(type, conflict)) {
      parse_error(ident, "Conflict typedef");
      return;
    }
  }

  if (conflict == NULL || type->kind != TY_STRUCT || type->struct_.info != NULL)
    add_typedef(name, type);

  consume(TK_SEMICOL, "`;' expected");
}

static Declaration *parse_global_var_decl(const Type *rawtype, int flag, const Type *type,
                                          Token *ident) {
  Vector *decls = NULL;
  for (;;) {
    if (type->kind == TY_VOID) {
      parse_error(ident, "`void' not allowed");
      return NULL;
    }

    if (!(type->kind == TY_PTR && type->pa.ptrof->kind == TY_FUNC))
      type = parse_type_suffix(type);

    intptr_t eval;
    if (find_enum_value(ident->ident, &eval)) {
      parse_error(ident, "`%.*s' is already defined", ident->ident->bytes, ident->ident->chars);
      return NULL;
    }
    VarInfo *varinfo = define_global(type, flag, ident, NULL);

    Initializer *init = NULL;
    if (match(TK_ASSIGN) != NULL)
      init = parse_initializer();
    varinfo->global.init = init;

    init = check_vardecl(type, ident, flag, init);
    VarDecl *decl = new_vardecl(type, ident, init, flag);
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
  int flag;
  Token *ident;
  if (parse_var_def(&rawtype, &type, &flag, &ident)) {
    if (ident == NULL) {
      if ((type->kind == TY_STRUCT ||
           (type->kind == TY_NUM && type->num.kind == NUM_ENUM)) &&
          match(TK_SEMICOL)) {
        // Just struct/union or enum definition.
      } else {
        parse_error(NULL, "Ident expected");
      }
      return NULL;
    }

    if (type->kind == TY_FUNC)
      return parse_defun(type, flag, ident);

    return parse_global_var_decl(rawtype, flag, type, ident);
  }
  if (match(TK_TYPEDEF)) {
    parse_typedef();
    return NULL;
  }
  parse_error(NULL, "Unexpected token");
  return NULL;
}

void parse(Vector *decls) {
  while (!match(TK_EOF)) {
    Declaration *decl = parse_declaration();
    if (decl != NULL)
      vec_push(decls, decl);
  }
}
