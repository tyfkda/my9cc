#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "emit.h"
#include "emit_code.h"
#include "ir_debug.h"
#include "lexer.h"
#include "parser.h"
#include "util.h"
#include "var.h"

////////////////////////////////////////////////

static void init_compiler(FILE *ofp) {
  init_lexer();
  init_emit(ofp);
  gvar_names = new_vector();
}

static void compile1(FILE *ifp, const char *filename, Vector *toplevel) {
  set_source_file(ifp, filename);
  parse(toplevel);
}

static const char LOCAL_LABEL_PREFIX[] = "--local-label-prefix=";

int main(int argc, char *argv[]) {
  int iarg;
  bool dump_ir = false;

  for (iarg = 1; iarg < argc; ++iarg) {
    char *arg = argv[iarg];
    if (*arg != '-')
      break;

    if (starts_with(arg, LOCAL_LABEL_PREFIX)) {
      set_local_label_prefix(&argv[iarg][sizeof(LOCAL_LABEL_PREFIX) - 1]);
    } else if (strcmp(arg, "--dump-ir") == 0) {
#if !defined(SELF_HOSTING) && !defined(__XV6)
      dump_ir = true;
#else
      fprintf(stderr, "option not supported: %s\n", arg);
      return 1;
#endif
    } else if (strcmp(arg, "--version") == 0) {
      show_version("cc1");
      return 0;
    } else {
      fprintf(stderr, "Unknown option: %s\n", arg);
      return 1;
    }
  }

  // Compile.
  init_compiler(stdout);

  toplevel = new_vector();
  if (iarg < argc) {
    for (int i = iarg; i < argc; ++i) {
      const char *filename = argv[i];
      FILE *ifp = fopen(filename, "r");
      if (ifp == NULL)
        error("Cannot open file: %s\n", filename);
      compile1(ifp, filename, toplevel);
    }
  } else {
    compile1(stdin, "*stdin*", toplevel);
  }
  if (compile_error_count <= 0)
    exit(1);

  gen(toplevel);

  if (!dump_ir) {
    emit_code(toplevel);
  } else {
#if !defined(SELF_HOSTING) && !defined(__XV6)
    do_dump_ir(toplevel);
#endif
  }

  return 0;
}
