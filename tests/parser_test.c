#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lexer.h"
#include "parser.h"
#include "table.h"
#include "type.h"
#include "util.h"

#include "ast.h"
#include "var.h"

void expect_parse_type_fn(const Type *(*parse_func)(int *, Token **), const Type *expected, const char *ident_expected, const char *source) {
  //pid_t pid = fork();
  //if (pid < 0) {
  //  fprintf(stderr, "fork failed\n");
  //  exit(1);
  //}
  //if (pid == 0) {
    set_source_file(NULL, source);
    print_type(stdout, expected);
    printf(" => ");
    fflush(stdout);

    set_source_string(source, "*test*", 1);
    int storage;
    Token *ident;
    const Type *actual = parse_func(&storage, &ident);
    if (actual == NULL && expected != NULL)
      error("%s: parsing type failed\n", source);

    const Token *end = fetch_token();
    if (end->kind != TK_EOF)
      parse_error(end, "EOF expected\n");

    if (!same_type(expected, actual)) {
      fprintf(stderr, "{{ ");
      print_type(stderr, actual);
      fprintf(stderr, "}}, ");
      error("type different\n");
    }
    if (ident_expected == NULL && ident != NULL)
      error("ident is not NULL (%.*s)\n", ident->ident->bytes, ident->ident->chars);
    if (ident_expected != NULL && ident == NULL)
      error("ident(%s) expected, but NULL\n", ident_expected);
    if (ident_expected != NULL && ident != NULL &&
        (ident->ident->bytes != (int)strlen(ident_expected) || strncmp(ident->ident->chars, ident_expected, strlen(ident_expected))))
      error("ident(%s) expected, but (%.*s)\n", ident_expected, ident->ident->bytes, ident->ident->chars);

    printf("OK\n");
  //  exit(0);
  //}

  /*
  int ec = -1;
  if (waitpid(pid, &ec, 0) < 0)
    error("wait failed");
  if (ec != 0)
    exit(1);
  */
}

void expect_parse_type(const Type *expected, const char *ident_expected, const char *source) {
  expect_parse_type_fn(parse_full_type, expected, ident_expected, source);
}

void test_parse_full_type(void) {
  expect_parse_type(get_fixnum_type(FX_INT, false, 0), NULL, "int");
  expect_parse_type(get_fixnum_type(FX_SHORT, false, 0), NULL, "short");
  expect_parse_type(get_fixnum_type(FX_SHORT, false, 0), NULL, "short int");
  expect_parse_type(get_fixnum_type(FX_SHORT, false, 0), NULL, "int short");
  expect_parse_type(get_fixnum_type(FX_LONG, false, 0), NULL, "long");
  expect_parse_type(get_fixnum_type(FX_LONG, false, 0), NULL, "long int");
  expect_parse_type(get_fixnum_type(FX_LLONG, false, 0), NULL, "long long");
  expect_parse_type(get_fixnum_type(FX_LONG, false, 0), NULL, "int long");
  expect_parse_type(ptrof(&tyVoid), NULL, "void*");
  expect_parse_type(arrayof(&tyInt, 3), "a", "int a[3]");
  expect_parse_type(arrayof(&tyChar, -1), NULL, "char[]");
  expect_parse_type(arrayof(arrayof(&tyInt, 3), 2), NULL, "int[2][3]");

  {
    Vector *param_types = new_vector();
    vec_push(param_types, get_fixnum_type(FX_LONG, false, 0));
    const Type *func = new_func_type(&tyInt, NULL, param_types, false);
    expect_parse_type(func, "func", "int func(long)");
  }
  {
    Vector *param_types = new_vector();
    vec_push(param_types, get_fixnum_type(FX_LONG, false, 0));
    const Type *funcptr = ptrof(new_func_type(&tyInt, NULL, param_types, false));
    expect_parse_type(funcptr, "func", "int(*func)(long)");
  }
  {
    const Type *funcptr = ptrof(new_func_type(&tyVoid, NULL, NULL, false));
    expect_parse_type(funcptr, "func", "void(*func)()");
  }

  expect_parse_type(arrayof(ptrof(&tyInt), 3), NULL, "int *[3]");
  expect_parse_type(ptrof(arrayof(&tyInt, 3)), NULL, "int (*)[3]");

  {
    Vector *param_types = new_vector();
    const Type *funcptr = ptrof(new_func_type(&tyInt, NULL, param_types, false));
    const Type *aofp = arrayof(funcptr, 4);
    expect_parse_type(aofp, NULL, "int(*[4])(void)");
  }

  {
    Vector *param_types2 = new_vector();
    vec_push(param_types2, &tyInt);
    const Type *param_funcptr = ptrof(new_func_type(&tyVoid, NULL, param_types2, false));

    Vector *param_types = new_vector();
    vec_push(param_types, &tyInt);
    vec_push(param_types, param_funcptr);
    const Type *functype = new_func_type(param_funcptr, NULL, param_types, false);
    expect_parse_type(functype, "signal", "void(*signal(int, void(*)(int)))(int)");
  }

#ifndef __NO_FLONUM
  expect_parse_type(&tyDouble, NULL, "double");
  expect_parse_type(&tyFloat, NULL, "float");
  expect_parse_type(&tyLDouble, NULL, "long double");
#endif
}

void runtest(void) {
  test_parse_full_type();
}






static Type *parse_declarator(Type *rawtype, Token **pident);

// <storage-class-specifier> ::= auto
//                             | register
//                             | static
//                             | extern
//                             | typedef

// <type-specifier> ::= void
//                    | char
//                    | short
//                    | int
//                    | long
//                    | float
//                    | double
//                    | signed
//                    | unsigned
//                    | <struct-or-union-specifier>
//                    | <enum-specifier>
//                    | <typedef-name>

// <type-qualifier> ::= const
//                    | volatile

// <struct-or-union-specifier> ::= <struct-or-union> <identifier> { {<struct-declaration>}+ }
//                               | <struct-or-union> { {<struct-declaration>}+ }
//                               | <struct-or-union> <identifier>

// <struct-or-union> ::= struct
//                     | union

// <struct-declaration> ::= {<specifier-qualifier>}* <struct-declarator-list>

// <enum-specifier> ::= enum <identifier> { <enumerator-list> }
//                    | enum { <enumerator-list> }
//                    | enum <identifier>

// <enumerator-list> ::= <enumerator>
//                     | <enumerator-list> , <enumerator>

// <enumerator> ::= <identifier>
//                | <identifier> = <constant-expression>

// <typedef-name> ::= <identifier>

// <declaration-specifier> ::= <storage-class-specifier>
//                           | <type-specifier>
//                           | <type-qualifier>
Type *parse_declaration_specifier(int *pstorage) {
  // 単体の型のパース
  return (Type*)parse_raw_type(pstorage);
}


// <pointer> ::= * {<type-qualifier>}* {<pointer>}?
static Type *parse_pointer(Type *type) {
  if (type == NULL)
    return NULL;

  for (;;) {
    const Token *tok;
    if ((tok = match(TK_CONST)) != NULL ||
        (tok = match(TK_VOLATILE)) != NULL) {
      // `type` might be pointing const value, so we cannot modify it.
      // TODO: Manage primitive types.
      // type->qualifier |= TQ_CONST;
      if (ptr_or_array(type))
        type->qualifier |= tok->kind == TK_CONST ? TQ_CONST : TQ_VOLATILE;
      continue;
    }

    if (!match(TK_MUL))
      break;
    type = ptrof(type);
  }

  return type;
}

// <direct-declarator> ::= <identifier>
//                       | ( <declarator> )
//                       | <direct-declarator> [ {<constant-expression>}? ]
//                       | <direct-declarator> ( <parameter-type-list> )
//                       | <direct-declarator> ( {<identifier>}* )
static Type *parse_direct_declarator_suffix(Type *type) {
  if (match(TK_LBRACKET)) {
    ssize_t length = -1;
    if (match(TK_RBRACKET)) {
      // Arbitrary size.
    } else {
      Expr *expr = parse_const();
      if (!(is_const(expr) && is_number(expr->type)))
        parse_error(expr->token, "syntax error");
      if (expr->fixnum <= 0)
        parse_error(expr->token, "Array size must be greater than 0, but %d", (int)expr->fixnum);
      length = expr->fixnum;
      consume(TK_RBRACKET, "`]' expected");
    }
    type = arrayof(parse_direct_declarator_suffix(type), length);
  } else if (match(TK_LPAR)) {
    bool vaargs;
    Vector *params = parse_funparams(&vaargs);
    const Type *rettype = parse_direct_declarator_suffix(type);

    Vector *param_types = extract_varinfo_types(params);
    type = new_func_type(rettype, params, param_types, vaargs);
  }
  return type;
}
static Type *parse_direct_declarator(Type *type, Token **pident) {
  Token *ident = NULL;
  if (match(TK_LPAR)) {
    Type *ret = type;
    Type *placeholder = calloc(1, sizeof(*placeholder));
    assert(placeholder != NULL);
    memcpy(placeholder, type, sizeof(*placeholder));

    type = parse_declarator(placeholder, &ident);
    consume(TK_RPAR, "`)' expected");

    Type *inner = parse_direct_declarator_suffix(ret);
    memcpy(placeholder, inner, sizeof(*placeholder));
  } else {
    ident = match(TK_IDENT);
    type = parse_direct_declarator_suffix(type);
  }

  if (pident != NULL)
    *pident = ident;

  return type;
}

// <declarator> ::= {<pointer>}? <direct-declarator>
static Type *parse_declarator(Type *rawtype, Token **pident) {
  Type *type = parse_pointer(rawtype);
  return parse_direct_declarator(type, pident);
}

// <init-declarator> ::= <declarator>
//                     | <declarator> = <initializer>
static Type *parse_init_declarator(Type *rawtype, Token **pident) {
  return parse_declarator(rawtype, pident);
  // TODO: Handle initializer.
}

// <declaration> ::=  {<declaration-specifier>}+ {<init-declarator>}* ;
const Type *parse_declaration(int *pstorage, Token **pident) {
  *pident = NULL;
  Type *type = parse_declaration_specifier(pstorage);
  if (type != NULL) {
    type = parse_init_declarator(type, pident);
  }
  return type;
}




void runtest2(void) {
  expect_parse_type_fn(parse_declaration, &tyInt, "a", "int a");
  expect_parse_type_fn(parse_declaration, ptrof(&tyVoid), "x", "void* x");
  expect_parse_type_fn(parse_declaration, ptrof(ptrof(&tyChar)), "pp", "char** pp");

  expect_parse_type_fn(parse_declaration, arrayof(&tyInt, 3), "xyz", "int xyz[3]");
  expect_parse_type_fn(parse_declaration, arrayof(&tyInt, -1), "abc", "int abc[]");
  expect_parse_type_fn(parse_declaration, arrayof(arrayof(&tyInt, 3), 2), "a2d", "int a2d[2][3]");
  expect_parse_type_fn(parse_declaration, arrayof(ptrof(&tyChar), 5), "ip", "char *ip[5]");
  expect_parse_type_fn(parse_declaration, ptrof(arrayof(&tyInt, 3)), "poa", "int (*poa)[3]");

  {
    Vector *param_types = new_vector();
    vec_push(param_types, get_fixnum_type(FX_LONG, false, 0));
    const Type *func = new_func_type(&tyInt, NULL, param_types, false);
    expect_parse_type_fn(parse_declaration, func, "func", "int func(long)");
  }
  {
    Vector *param_types = new_vector();
    vec_push(param_types, &tyChar);
    vec_push(param_types, get_fixnum_type(FX_SHORT, false, 0));
    const Type *funcptr = ptrof(new_func_type(&tyInt, NULL, param_types, false));
    expect_parse_type_fn(parse_declaration, funcptr, "func", "int(*func)(char, short)");
  }
  {
    // 引数＆戻り値の関数型
    Vector *param_types2 = new_vector();
    vec_push(param_types2, &tyInt);
    const Type *param_funcptr = ptrof(new_func_type(&tyVoid, NULL, param_types2, false));

    Vector *param_types = new_vector();
    vec_push(param_types, &tyInt);
    vec_push(param_types, param_funcptr);
    const Type *functype = new_func_type(param_funcptr, NULL, param_types, false);
    expect_parse_type_fn(parse_declaration, functype, "signal", "void(*signal(int, void(*)(int)))(int)");
  }


  expect_parse_type_fn(parse_declaration, arrayof(ptrof(&tyInt), 3), NULL, "int *[3]");
  expect_parse_type_fn(parse_declaration, ptrof(arrayof(&tyInt, 3)), NULL, "int (*)[3]");

  {
    Vector *param_types = new_vector();
    const Type *funcptr = ptrof(new_func_type(&tyInt, NULL, param_types, false));
    const Type *aofp = arrayof(funcptr, 4);
    expect_parse_type_fn(parse_declaration, aofp, NULL, "int(*[4])(void)");
  }

}



int main(void) {
  init_lexer();

  //runtest();
  runtest2();
  return 0;
}
