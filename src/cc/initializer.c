#include "initializer.h"

#include <assert.h>
#include <stddef.h>  // size_t
#include <stdint.h>  // intptr_t
#include <stdlib.h>  // malloc

#include "ast.h"
#include "lexer.h"
#include "table.h"
#include "type.h"
#include "util.h"
#include "var.h"

#include "parser.h"  // TODO: Avoid circular dependencies.

void fix_array_size(Type *type, Initializer *init) {
  assert(init != NULL);
  assert(type->kind == TY_ARRAY);

  bool is_str = (is_char_type(type->pa.ptrof) &&
                 init->kind == IK_SINGLE &&
                 init->single->kind == EX_STR);
  if (!is_str && init->kind != IK_MULTI) {
    parse_error(init->token, "Error initializer");
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
    assert(!is_str || init->single->kind == EX_STR);
    size_t init_len = is_str ? init->single->str.size : (size_t)init->multi->len;
    if (init_len > arr_len)
      parse_error(NULL, "Initializer more than array size");
  }
}

// Returns created global variable info.
VarInfo *str_to_char_array(const Type *type, Initializer *init) {
  assert(type->kind == TY_ARRAY && is_char_type(type->pa.ptrof));
  const Token *ident = alloc_ident(alloc_label(), NULL, NULL);
  VarInfo *varinfo = scope_add(curscope, ident, type, VS_STATIC);
  if (is_global_scope(curscope)) {
    Vector *decls = new_vector();
    vec_push(decls, new_vardecl(varinfo->type, ident, init, varinfo->storage));
    vec_push(toplevel, new_decl_vardecl(decls));
  }
  varinfo->global.init = init;
  return varinfo;
}

Expr *str_to_char_array_var(Expr *str) {
  if (str->kind != EX_STR)
    return str;
  const Type* type = str->type;
  Initializer *init = malloc(sizeof(*init));
  init->kind = IK_SINGLE;
  init->single = str;
  init->token = str->token;

  VarInfo *gvarinfo = str_to_char_array(type, init);
  Scope *scope;
  VarInfo *varinfo = scope_find(curscope, gvarinfo->name, &scope);
  return new_expr_variable(varinfo->name, type, str->token, scope);
}

// Convert string literal to global char-array variable reference.
Initializer *convert_str_to_ptr_initializer(const Type *type, Initializer *init) {
  assert(type->kind == TY_ARRAY && is_char_type(type->pa.ptrof));
  VarInfo *varinfo = str_to_char_array(type, init);
  Initializer *init2 = malloc(sizeof(*init2));
  init2->kind = IK_SINGLE;
  init2->single = new_expr_variable(varinfo->name, type, NULL, global_scope);
  init2->token = init->token;
  return init2;
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
    if (init_elem->kind == IK_DOT)
      parse_error(NULL, "dot initializer for array");
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
      if (i < len && init_elem->arr.index->kind != EX_FIXNUM)
        parse_error(NULL, "Constant value expected");
      if ((size_t)i > lastStartIndex) {
        size_t *range = malloc(sizeof(size_t) * 3);
        range[0] = lastStart;
        range[1] = lastStartIndex;
        range[2] = index - lastStart;
        vec_push(ranges, range);
      }
      if (i >= len)
        break;
      lastStart = index = init_elem->arr.index->fixnum;
      lastStartIndex = i;
    } else if (init_elem->kind == IK_DOT)
      parse_error(NULL, "dot initializer for array");
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
      if (start < q[0] + q[2])
        parse_error(NULL, "Initializer for array overlapped");
    }
    for (size_t j = 0; j < count; ++j) {
      Initializer *elem = init->multi->data[index + j];
      if (j == 0 && index != start && elem->kind != IK_ARR) {
        Initializer *arr = malloc(sizeof(*arr));
        arr->kind = IK_ARR;
        Fixnum n = start;
        arr->arr.index = new_expr_fixlit(&tyInt, NULL, n);
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

Initializer *flatten_initializer(const Type *type, Initializer *init) {
  if (init == NULL)
    return NULL;

  switch (type->kind) {
  case TY_STRUCT:
    if (init->kind == IK_MULTI) {
      const StructInfo *sinfo = type->struct_.info;
      int n = sinfo->members->len;
      int m = init->multi->len;
      if (n <= 0) {
        if (m > 0)
          parse_error(init->token, "Initializer for empty struct");
        return init;
      }
      if (sinfo->is_union && m > 1)
        parse_error(((Initializer*)init->multi->data[1])->token, "Initializer for union more than 1");

      Initializer **values = malloc(sizeof(Initializer*) * n);
      for (int i = 0; i < n; ++i)
        values[i] = NULL;

      int index = 0;
      for (int i = 0; i < m; ++i) {
        Initializer *value = init->multi->data[i];
        if (value->kind == IK_ARR)
          parse_error(NULL, "indexed initializer for struct");

        if (value->kind == IK_DOT) {
          const Name *name = value->dot.name;
          index = var_find(sinfo->members, name);
          if (index >= 0) {
            value = value->dot.value;
          } else {
            Vector *stack = new_vector();
            if (search_from_anonymous(type, name, NULL, stack) == NULL)
              parse_error(value->token, "`%.*s' is not member of struct", name->bytes, name->chars);

            index = (intptr_t)stack->data[0];
            Vector *multi = new_vector();
            vec_push(multi, value);
            Initializer *init2 = malloc(sizeof(*init2));
            init2->kind = IK_MULTI;
            init2->multi = multi;
            value = init2;
          }
        }
        if (index >= n)
          parse_error(NULL, "Too many init values");

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
