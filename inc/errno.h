#pragma once

//extern int errno;

extern int *__errno_location(void) /*__THROW __attribute__((__const__))*/;
#define errno (*__errno_location())
