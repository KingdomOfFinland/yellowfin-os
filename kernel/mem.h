#ifndef MEM_H
#define MEM_H

#include <stdint.h>

void     mem_init(uint32_t magic, uint32_t mbi);   /* parse and log the memory map */
uint32_t mem_usable_kb(void);

#endif