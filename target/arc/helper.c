#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

void HELPER(halt)(CPUArcState *env)
{
    exit(0);
}
