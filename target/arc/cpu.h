#ifndef ARC_CPU_H
#define ARC_CPU_H
#define CPU_RESOLVING_TYPE TYPE_ARC_CPU

#include "qemu/typedefs.h"
#include "cpu-qom.h"
#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"

struct CPUArchState {
};

struct ArchCPU {
    CPUState parent_obj;
    CPUArchState env;

};

#endif
