#pragma once

struct FILE {
  int fd;
  int wp;

  //unsigned char buf[512 - 8];
  unsigned char buf[32];
};
