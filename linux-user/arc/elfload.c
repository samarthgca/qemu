#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"

const char *get_elf_cpu_model(uint32_t eflags)
{
    return "any";
}
