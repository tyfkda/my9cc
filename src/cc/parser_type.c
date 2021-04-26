#include "parser.h"

#include <assert.h>
#include <inttypes.h>  // PRIdPTR

#include "ast.h"
#include "lexer.h"
#include "table.h"
#include "type.h"
#include "util.h"
#include "var.h"

static void define_enum_member(const Type *type, const Token *ident, int value) {
  VarInfo *varinfo = scope_add(curscope, ident, type, VS_ENUM_MEMBER);
  varinfo->enum_member.value = value;
}

static void parse_enum_members(const Type *type) {
  assert(type != NULL && type->kind == TY_FIXNUM && type->fixnum.kind == FX_ENUM);
  const Type *ctype = qualified_type(type, TQ_CONST);
  int value = 0;
  for (;;) {
    Token *token = consume(TK_IDENT, "ident expected");
    if (match(TK_ASSIGN)) {
      Expr *expr = parse_const();
      value = expr->fixnum;
    }

    if (scope_find(global_scope, token->ident, NULL) != NULL) {
      parse_error(token, "`%.*s' is already defined",
                  token->ident->bytes, token->ident->chars);
    } else {
      define_enum_member(ctype, token, value);
    }
    ++value;

    if (match(TK_COMMA))
      ;
    if (match(TK_RBRACE))
      break;
  }
}

static Type *parse_enum(void) {
  Token *ident = match(TK_IDENT);
  Type *type = ident != NULL ? find_enum(curscope, ident->ident) : NULL;
  if (match(TK_LBRACE)) {
    if (type != NULL)
      parse_error(ident, "Duplicate enum type");
    type = define_enum(curscope, ident != NULL ? ident->ident : NULL);
    if (!match(TK_RBRACE))
      parse_enum_members(type);
  } else {
    if (type == NULL)
      parse_error(ident, "Unknown enum type");
  }
  return type;
}

// Parse struct or union definition `{...}`
static StructInfo *parse_struct(bool is_union) {
  Vector *members = new_vector();
  for (;;) {
    if (match(TK_RBRACE))
      break;

    Type *rawType = NULL;
    for (;;) {
      const Type *type;
      int storage;
      Token *ident;
      if (!parse_var_def(&rawType, &type, &storage, &ident))
        parse_error(NULL, "type expected");
      not_void(type, NULL);
      if (type->kind == TY_STRUCT) {
        ensure_struct((Type*)type, ident, curscope);
        // Allow ident to be null for anonymous struct member.
      } else {
        if (ident == NULL)
          parse_error(NULL, "`ident' expected");
      }
      const Name *name = ident != NULL ? ident->ident : NULL;
      var_add(members, name, type, storage, ident);

      if (match(TK_COMMA))
        continue;
      consume(TK_SEMICOL, "`;' expected");
      break;
    }
  }
  return create_struct_info(members, is_union);
}

typedef struct {
  int storage, qualifier;
  int unsigned_num, signed_num;
  int char_num, short_num, int_num, long_num;
#ifndef __NO_FLONUM
  int float_num, double_num;
#endif
} TypeCombination;

static const enum FixnumKind kLongKinds[] = {
  FX_INT, FX_LONG, FX_LLONG,
};

#define ASSERT_PARSE_ERROR(cond, tok, ...)  do { if (!(cond)) parse_error(tok, __VA_ARGS__); } while (0)

static void check_type_combination(const TypeCombination *tc, const Token *tok) {
  if (tc->unsigned_num > 1 || tc->signed_num > 1 ||
      tc->char_num > 1 || tc->short_num > 1 || tc->int_num > 1 ||
      tc->long_num >= (int)(sizeof(kLongKinds) / sizeof(*kLongKinds)) ||
      ((tc->char_num > 0) + (tc->short_num > 0) + (tc->long_num > 0) > 1)
#ifndef __NO_FLONUM
      || tc->float_num > 1 || tc->double_num > 1 ||
      ((tc->float_num > 0 || tc->double_num > 0) &&
       (tc->char_num > 0 || tc->short_num > 0 || tc->int_num > 0 || tc->long_num > 0 ||
        tc->unsigned_num > 0 || tc->signed_num > 0) &&
       !(tc->double_num == 1 && tc->float_num <= 0 && tc->long_num <= 1 &&
         tc->char_num <= 0 && tc->short_num <= 0 && tc->int_num <= 0 &&
         tc->unsigned_num <= 0 && tc->signed_num <= 0)
      )
#endif
  ) {
    parse_error(tok, "Illegal type combination");
  }
}

static bool no_type_combination(const TypeCombination *tc, int storage_mask, int qualifier_mask) {
  return tc->unsigned_num == 0 && tc->signed_num == 0 &&
      tc->char_num == 0 && tc->short_num == 0 && tc->int_num == 0 && tc->long_num == 0 &&
      (tc->storage & storage_mask) == 0 && (tc->qualifier & qualifier_mask) == 0
#ifndef __NO_FLONUM
      && tc->float_num == 0 && tc->double_num == 0
#endif
      ;
}

Type *parse_raw_type(int *pstorage) {
  Type *type = NULL;

  TypeCombination tc = {0};
  Token *tok = NULL;
  for (;;) {
    if (tok != NULL)
      check_type_combination(&tc, tok);  // Check for last token
    tok = match(-1);
    if (tok->kind == TK_UNSIGNED) {
      ++tc.unsigned_num;
      continue;
    }
    if (tok->kind == TK_SIGNED) {
      ++tc.signed_num;
      continue;
    }
    if (tok->kind == TK_STATIC) {
      ASSERT_PARSE_ERROR(tc.storage == 0, tok, "multiple storage specified");
      tc.storage |= VS_STATIC;
      continue;
    }
    if (tok->kind == TK_EXTERN) {
      ASSERT_PARSE_ERROR(tc.storage == 0, tok, "multiple storage specified");
      tc.storage |= VS_EXTERN;
      continue;
    }
    if (tok->kind == TK_TYPEDEF) {
      ASSERT_PARSE_ERROR(tc.storage == 0, tok, "multiple storage specified");
      tc.storage |= VS_TYPEDEF;
      continue;
    }
    if (tok->kind == TK_CONST) {
      ASSERT_PARSE_ERROR(tc.qualifier == 0, tok, "multiple qualifier specified");
      tc.qualifier |= TQ_CONST;
      continue;
    }
    if (tok->kind == TK_VOLATILE) {
      ASSERT_PARSE_ERROR(tc.qualifier == 0, tok, "multiple qualifier specified");
      tc.qualifier |= TQ_VOLATILE;
      continue;
    }
    if (tok->kind == TK_CHAR) {
      ++tc.char_num;
      continue;
    }
    if (tok->kind == TK_SHORT) {
      ++tc.short_num;
      continue;
    }
    if (tok->kind == TK_INT) {
      ++tc.int_num;
      continue;
    }
    if (tok->kind == TK_LONG) {
      ++tc.long_num;
      continue;
    }
#ifndef __NO_FLONUM
    if (tok->kind == TK_FLOAT) {
      ++tc.float_num;
      continue;
    }
    if (tok->kind == TK_DOUBLE) {
      ++tc.double_num;
      continue;
    }
#endif

    if (type != NULL) {
      unget_token(tok);
      break;
    }

    if (tok->kind == TK_STRUCT ||
        tok->kind == TK_UNION) {
      if (!no_type_combination(&tc, 0, 0))
        parse_error(tok, "Illegal type combination");

      bool is_union = tok->kind == TK_UNION;
      const Name *name = NULL;
      Token *ident;
      if ((ident = match(TK_IDENT)) != NULL)
        name = ident->ident;

      StructInfo *sinfo = NULL;
      if (match(TK_LBRACE)) {  // Definition
        sinfo = parse_struct(is_union);
        if (name != NULL) {
          Scope *scope;
          StructInfo *exist = find_struct(curscope, name, &scope);
          if (exist != NULL && scope == curscope)
            parse_error(ident, "`%.*s' already defined", name->bytes, name->chars);
          define_struct(curscope, name, sinfo);
        }
      } else {
        if (name != NULL) {
          sinfo = find_struct(curscope, name, NULL);
          if (sinfo != NULL) {
            if (sinfo->is_union != is_union)
              parse_error(tok, "Wrong tag for `%.*s'", name->bytes, name->chars);
          }
        }
      }

      if (name == NULL && sinfo == NULL)
        parse_error(NULL, "Illegal struct/union usage");

      type = create_struct_type(sinfo, name, tc.qualifier);
    } else if (tok->kind == TK_ENUM) {
      if (!no_type_combination(&tc, 0, 0))
        parse_error(tok, "Illegal type combination");

      type = parse_enum();
    } else if (tok->kind == TK_IDENT) {
      if (no_type_combination(&tc, 0, 0)) {
        Token *ident = tok;
        type = find_typedef(curscope, ident->ident, NULL);
      }
    } else if (tok->kind == TK_VOID) {
      type = (Type*)&tyVoid;
    }
    if (type == NULL) {
      unget_token(tok);
      break;
    }
  }

  if (type == NULL && !no_type_combination(&tc, ~0, ~0)) {
#ifndef __NO_FLONUM
    if (tc.float_num > 0) {
      type = (Type*)&tyFloat;
    } else if (tc.double_num > 0) {
      type = (Type*)(tc.double_num > 1 ? &tyLDouble : &tyDouble);
    } else
#endif
    {
      enum FixnumKind fk =
          (tc.char_num > 0) ? FX_CHAR :
          (tc.short_num > 0) ? FX_SHORT : kLongKinds[tc.long_num];
      type = (Type*)get_fixnum_type(fk, tc.unsigned_num > 0, tc.qualifier);
    }
  }

  if (pstorage != NULL)
    *pstorage = tc.storage;

  return type;
}

const Type *parse_type_modifier(const Type *type) {
  if (type == NULL)
    return NULL;

  for (;;) {
    if (match(TK_CONST))
      type = qualified_type(type, TQ_CONST);
    if (match(TK_MUL))
      type = ptrof(type);
    else
      break;
  }

  return type;
}

const Type *parse_type_suffix(const Type *type) {
  if (type == NULL)
    return NULL;

  if (!match(TK_LBRACKET))
    return type;
  ssize_t length = -1;
  if (match(TK_RBRACKET)) {
    // Arbitrary size.
  } else {
    Expr *expr = parse_const();
    if (expr->fixnum <= 0)
      parse_error(expr->token, "Array size must be greater than 0, but %" PRIdPTR, expr->fixnum);
    length = expr->fixnum;
    consume(TK_RBRACKET, "`]' expected");
  }
  return arrayof(parse_type_suffix(type), length);
}
