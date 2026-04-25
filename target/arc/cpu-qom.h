#ifndef QEMU_ARC_CPU_QOM_H
#define QEMU_ARC_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_ARC_CPU "arc-cpu"

OBJECT_DECLARE_CPU_TYPE(ArcCPU, ArcCPUClass, ARC_CPU)

#define ARC_CPU_TYPE_SUFFIX "-" TYPE_ARC_CPU
#define ARC_CPU_TYPE_NAME(model) model ARC_CPU_TYPE_SUFFIX

#endif
