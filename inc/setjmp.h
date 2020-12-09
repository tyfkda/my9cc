#pragma once

#include <stdint.h>  // uintptr_t

//typedef uintptr_t jmp_buf[8];
typedef uintptr_t jmp_buf[200 / 8];  // GCC

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int result);
