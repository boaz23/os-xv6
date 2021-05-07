#include "kernel/param.h"

int bsem_alloc();
void bsem_free(int);
void bsem_down(int);
void bsem_up(int);
