#include "lexer.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>  // malloc
#include <string.h>
#include <sys/types.h>  // ssize_t

#include "table.h"
#include "util.h"

#define MAX_LOOKAHEAD  (2)
#define MAX_ERROR_COUNT  (25)

static const struct {
  const char *str;
  enum TokenKind kind;
} kReservedWords[] = {
  {"if", TK_IF},
  {"else", TK_ELSE},
  {"switch", TK_SWITCH},
  {"case", TK_CASE},
  {"default", TK_DEFAULT},
  {"do", TK_DO},
  {"while", TK_WHILE},
  {"for", TK_FOR},
  {"break", TK_BREAK},
  {"continue", TK_CONTINUE},
  {"goto", TK_GOTO},
  {"return", TK_RETURN},
  {"void", TK_VOID},
  {"char", TK_CHAR},
  {"short", TK_SHORT},
  {"int", TK_INT},
  {"long", TK_LONG},
  {"const", TK_CONST},
  {"unsigned", TK_UNSIGNED},
  {"static", TK_STATIC},
  {"extern", TK_EXTERN},
  {"struct", TK_STRUCT},
  {"union", TK_UNION},
  {"enum", TK_ENUM},
  {"sizeof", TK_SIZEOF},
  {"typedef", TK_TYPEDEF},
  {"__asm", TK_ASM},
};

static const struct {
  const char ident[4];
  enum TokenKind kind;
} kMultiOperators[] = {
  {"<<=", TK_LSHIFT_ASSIGN},
  {">>=", TK_RSHIFT_ASSIGN},
  {"...", TK_DOTDOTDOT},
  {"==", TK_EQ},
  {"!=", TK_NE},
  {"<=", TK_LE},
  {">=", TK_GE},
  {"+=", TK_ADD_ASSIGN},
  {"-=", TK_SUB_ASSIGN},
  {"*=", TK_MUL_ASSIGN},
  {"/=", TK_DIV_ASSIGN},
  {"%=", TK_MOD_ASSIGN},
  {"&=", TK_AND_ASSIGN},
  {"|=", TK_OR_ASSIGN},
  {"^=", TK_HAT_ASSIGN},
  {"++", TK_INC},
  {"--", TK_DEC},
  {"->", TK_ARROW},
  {"&&", TK_LOGAND},
  {"||", TK_LOGIOR},
  {"<<", TK_LSHIFT},
  {">>", TK_RSHIFT},
};

static const char kSingleOperatorTypeMap[128] = {  // enum TokenKind
  ['+'] = TK_ADD,
  ['-'] = TK_SUB,
  ['*'] = TK_MUL,
  ['/'] = TK_DIV,
  ['%'] = TK_MOD,
  ['&'] = TK_AND,
  ['|'] = TK_OR,
  ['^'] = TK_HAT,
  ['<'] = TK_LT,
  ['>'] = TK_GT,
  ['!'] = TK_NOT,
  ['('] = TK_LPAR,
  [')'] = TK_RPAR,
  ['{'] = TK_LBRACE,
  ['}'] = TK_RBRACE,
  ['['] = TK_LBRACKET,
  [']'] = TK_RBRACKET,
  ['='] = TK_ASSIGN,
  [':'] = TK_COLON,
  [';'] = TK_SEMICOL,
  [','] = TK_COMMA,
  ['.'] = TK_DOT,
  ['?'] = TK_QUESTION,
  ['~'] = TK_TILDA,
};

typedef struct {
  FILE *fp;
  const char *filename;
  Line *line;
  const char *p;
  Token *fetched[MAX_LOOKAHEAD];
  int idx;
  int lineno;
} Lexer;

static Lexer lexer;
static Table reserved_word_table;
int compile_error_count;

static void show_error_line(const char *line, const char *p, int len) {
  fprintf(stderr, "%s\n", line);
  size_t pos = p - line;
  if (pos <= strlen(line)) {
    for (size_t i = 0; i < pos; ++i)
      fputc(line[i] == '\t' ? '\t' : ' ', stderr);
    fprintf(stderr, "^");
    for (int i = 1; i < len; ++i)
      fprintf(stderr, "~");
    fprintf(stderr, "\n");
  }
}

static void lex_error(const char *p, const char *fmt, ...) {
  fprintf(stderr, "%s(%d): ", lexer.filename, lexer.lineno);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");

  show_error_line(lexer.line->buf, p, 1);

  if (++compile_error_count >= MAX_ERROR_COUNT)
    exit(1);
}

void parse_error(const Token *token, const char *fmt, ...) {
  if (token == NULL)
    token = fetch_token();
  if (token != NULL && token->line != NULL) {
    fprintf(stderr, "%s(%d): ", token->line->filename, token->line->lineno);
  }

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");

  if (token != NULL && token->line != NULL && token->begin != NULL)
    show_error_line(token->line->buf, token->begin, token->end - token->begin);

  if (++compile_error_count >= MAX_ERROR_COUNT)
    exit(1);
}

static Token *alloc_token(enum TokenKind kind, const char *begin, const char *end) {
  Token *token = malloc(sizeof(*token));
  token->kind = kind;
  token->line = lexer.line;
  token->begin = begin;
  token->end = end;
  return token;
}

Token *alloc_ident(const Name *name, const char *begin, const char *end) {
  Token *tok = alloc_token(TK_IDENT, begin, end);
  tok->ident = name;
  return tok;
}

static void init_reserved_word_table(void) {
  table_init(&reserved_word_table);

  // Reserved words.
  for (int i = 0, n = (int)(sizeof(kReservedWords) / sizeof(*kReservedWords)); i < n; ++i) {
    const Name *key = alloc_name(kReservedWords[i].str, NULL, false);
    table_put(&reserved_word_table, key, (void*)(intptr_t)kReservedWords[i].kind);
  }

  // Multi-char operators.
  for (int i = 0, n = (int)(sizeof(kMultiOperators) / sizeof(*kMultiOperators)); i < n; ++i) {
    const Name *key = alloc_name(kMultiOperators[i].ident, NULL, false);
    table_put(&reserved_word_table, key, (void*)(intptr_t)kMultiOperators[i].kind);
  }
}

static enum TokenKind reserved_word(const Name *name) {
  enum TokenKind result = -1;
  void *ptr = table_get(&reserved_word_table, name);
  if (ptr != NULL)
    result = (enum TokenKind)ptr;
  return result;
}

static char backslash(char c) {
  switch (c) {
  case '0':  return '\0';
  case 'n':  return '\n';
  case 't':  return '\t';
  case 'r':  return '\r';
  case 'f':  return '\f';
  case 'v':  return '\v';
  default:   return c;
  }
}

void init_lexer(void) {
  init_reserved_word_table();
}

void set_source_file(FILE *fp, const char *filename) {
  lexer.fp = fp;
  lexer.filename = filename;
  lexer.line = NULL;
  lexer.p = "";
  lexer.idx = -1;
  lexer.lineno = 0;
}

void set_source_string(const char *line, const char *filename, int lineno) {
  Line *p = malloc(sizeof(*p));
  p->filename = lexer.filename;
  p->buf = line;
  p->lineno = lineno;

  lexer.fp = NULL;
  lexer.filename = filename;
  lexer.line = p;
  lexer.p = line;
  lexer.idx = -1;
  lexer.lineno = lineno;
}

const char *get_lex_p(void) {
  if (lexer.idx < 0)
    return lexer.p;
  else
    return lexer.fetched[lexer.idx]->begin;
}

static int scan_linemarker(const char *line, long *pnum, char **pfn, int *pflag) {
  const char *p = line;
  if (p[0] != '#' || p[1] != ' ')
    return 0;
  p += 2;

  int n = 0;
  const char *next = p;
  long num = strtol(next, (char**)&next, 10);
  if (next > p) {
    ++n;
    *pnum = num;
    p = next;

    if (p[0] == ' ' && p[1] == '"') {
      p += 2;
      const char *q = strchr(p, '"');
      if (q != NULL) {
        ++n;
        *pfn = strndup_(p, q - p);
        p = q + 1;

        if (p[0] == ' ') {
          p += 1;
          next = p;
          int flag = strtol(next, (char**)&next, 10);
          if (next > p) {
            ++n;
            *pflag = flag;
          }
        }
      }
    }
  }
  return n;
}

static void read_next_line(void) {
  if (lexer.fp == NULL) {
    lexer.p = NULL;
    lexer.line = NULL;
    return;
  }

  char *line = NULL;
  size_t capa = 0;
  for (;;) {
    ssize_t len = getline_(&line, &capa, lexer.fp, 0);
    if (len == EOF) {
      lexer.p = NULL;
      lexer.line = NULL;
      return;
    }
    while (len > 0 && line[len - 1] == '\\')  // Continue line.
      len = getline_(&line, &capa, lexer.fp, len - 1);  // -1 for overwrite on '\'

    if (line[0] != '#')
      break;

    // linemarkers: # linenum filename flags
    long num = -1;
    char *fn;
    int flag = -1;
    int n = scan_linemarker(line, &num, &fn, &flag);
    if (n >= 2) {
      lexer.lineno = num - 1;
      lexer.filename = fn;
    }
  }

  Line *p = malloc(sizeof(*p));
  p->filename = lexer.filename;
  p->buf = line;
  p->lineno = ++lexer.lineno;
  lexer.line = p;
  lexer.p = lexer.line->buf;
}

static const char *skip_block_comment(const char *p) {
  for (;;) {
    char c = *p++;
    if (c == '\0') {
      read_next_line();
      p = lexer.p;
      if (p == NULL)
        return NULL;
      continue;
    } else if (c == '*' && *p == '/')
      return p + 1;
  }
}

static const char *skip_line_comment(void) {
  read_next_line();
  return lexer.p;
}

static const char *skip_whitespace_or_comment(const char *p) {
  for (;;) {
    char c = *p++;
    if (c == '\0') {
      read_next_line();
      p = lexer.p;
      if (p == NULL)
        return NULL;
      continue;
    } else if (isspace(c)) {
      continue;
    } else if (c == '/') {
      if (*p == '*') {
        p = skip_block_comment(p + 1);
        if (p == NULL) {
          lex_error(p, "Block comment not closed");
          return NULL;
        }
        continue;
      } else if (*p == '/') {
        p = skip_line_comment();
        if (p == NULL)
          return NULL;
        continue;
      }
    }
    break;
  }
  return p - 1;
}

static Token *read_num(const char **pp) {
  const char *start = *pp, *p = start;
  int base = 10;
  bool is_unsigned = false;
  if (*p == '0') {
    if (p[1] == 'x') {
      base = 16;
      p += 2;
    } else {
      base = 8;
    }
  }
  const char *q = p;
  long val = strtol(p, (char**)&p, base);
  if (p == q) {
    lex_error(p, "Illegal literal");
    val = 0;
  }

  enum TokenKind tt = TK_INTLIT;
  for (;;) {
    if (tolower(*p) == 'u') {
      is_unsigned = true;
      ++p;
    } else if (tolower(*p) == 'l') {
      tt = TK_LONGLIT;
      ++p;
    } else {
      break;
    }
  }
  Token *tok = alloc_token(tt + (is_unsigned ? (TK_UINTLIT - TK_INTLIT) : 0), start, p);
  tok->value = val;
  *pp = p;
  return tok;
}

const char *read_ident(const char *p) {
  if (!isalpha(*p) && *p != '_')
    return NULL;

  const char *q;
  for (q = p + 1; ; ++q) {
    if (!(isalnum(*q) || *q == '_'))
      break;
  }
  return q;
}

static Token *read_char(const char **pp) {
  const char *p = *pp;
  const char *begin = p++;
  char c = *p;
  if (c == '\'') {
    lex_error(p, "Empty character");
    c = '\0';
    ++p;
  } else {
    if (c == '\\') {
      c = *(++p);
      if (c == '\0')
        --p;  // Raise error with no close quote.
      else
        c = backslash(c);
    }
    if (*(++p) != '\'')
      lex_error(p, "Character not closed");
    else
      ++p;
  }

  Token *tok = alloc_token(TK_CHARLIT, begin, p);
  tok->value = c;
  *pp = p;
  return tok;
}

static Token *read_string(const char **pp) {
  const int ADD = 16;
  const char *p = *pp;
  const char *begin, *end;
  size_t capa = 16, size = 0;
  char *str = malloc(capa);
  for (;;) {
    begin = p++;  // Skip first '"'
    for (char c; (c = *p++) != '"'; ) {
      if (c == '\0') {
        lex_error(--p, "String not closed");
        break;
      }
      if (size + 1 >= capa) {
        capa += ADD;
        char *newstr = realloc(str, capa);
        if (newstr == NULL) {
          lex_error(p, "Out of memory");
          break;
        }
        str = newstr;
      }

      if (c == '\\') {
        c = *p++;
        if (c == '\0') {
          lex_error(--p, "String not closed");
          break;
        }
        c = backslash(c);
      }
      assert(size < capa);
      str[size++] = c;
    }
    end = p;

    // Continue string literal when next character is '"'
    p = skip_whitespace_or_comment(p);
    if (p == NULL || *p != '"')
      break;
  }
  assert(size < capa);
  str[size++] = '\0';
  Token *tok = alloc_token(TK_STR, begin, end);
  tok->str.buf = str;
  tok->str.size = size;
  *pp = p;
  return tok;
}

static Token *get_op_token(const char **pp) {
  const char *p = *pp;
  char c = *p;
  if (c >= 0 /*&& c < sizeof(kSingleOperatorTypeMap)*/) {
    enum TokenKind single = kSingleOperatorTypeMap[(int)c];
    if (single != 0) {
      int n;
      for (n = 1; n < 3; ++n) {
        if (kSingleOperatorTypeMap[(int)p[n]] == 0)
          break;
      }

      for (int len = n; len > 1; --len) {
        const Name *op = alloc_name(p, p + len, false);
        enum TokenKind kind = reserved_word(op);
        if ((int)kind != -1) {
          const char *q = p + len;
          *pp = q;
          return alloc_token(kind, p, q);
        }
      }

      const char *q = p + 1;
      *pp = q;
      return alloc_token(single, p, q);
    }
  }
  return NULL;
}

static Token *get_token(void) {
  static Token kEofToken = {.kind = TK_EOF};

  const char *p = lexer.p;
  if (p == NULL)
    return &kEofToken;

  p = skip_whitespace_or_comment(p);
  if (p == NULL)
    return &kEofToken;

  Token *tok = NULL;
  const char *begin = p;
  const char *ident_end = read_ident(p);
  if (ident_end != NULL) {
    const Name *name = alloc_name(begin, ident_end, false);
    enum TokenKind word = reserved_word(name);
    if ((int)word != -1) {
      tok = alloc_token(word, begin, ident_end);
    } else {
      tok = alloc_ident(name, begin, ident_end);
    }
    p = ident_end;
  } else if ((tok = get_op_token(&p)) != NULL) {
    // Ok.
  } else if (isdigit(*p)) {
    tok = read_num(&p);
  } else if (*p == '\'') {
    tok = read_char(&p);
  } else if (*p == '"') {
    tok = read_string(&p);
  } else {
    lex_error(p, "Unexpected character `%c'(%d)", *p, *p);
    return &kEofToken;
  }

  assert(tok != NULL);
  lexer.p = p;
  return tok;
}

Token *fetch_token(void) {
  if (lexer.idx < 0) {
    lexer.idx = 0;
    lexer.fetched[0] = get_token();
  }
  return lexer.fetched[lexer.idx];
}

Token *match(enum TokenKind kind) {
  Token *tok = fetch_token();
  if (tok->kind != kind && (int)kind != -1)
    return NULL;
  if (tok->kind != TK_EOF)
    --lexer.idx;
  return tok;
}

Token *consume(enum TokenKind kind, const char *error) {
  Token *tok = match(kind);
  if (tok == NULL)
    parse_error(tok, error);
  return tok;
}

void unget_token(Token *token) {
  ++lexer.idx;
  assert(lexer.idx < MAX_LOOKAHEAD);
  lexer.fetched[lexer.idx] = token;
}
