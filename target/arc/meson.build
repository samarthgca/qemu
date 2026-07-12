arc_ss = ss.source_set()
arc_ss.add(decodetree.process('insn.decode'))
arc_ss.add(files('translate.c', 'cpu.c', 'gdbstub.c', 'helper.c'))
arc_system_ss = ss.source_set()
arc_system_ss.add(files())

target_arch += {'arc': arc_ss}
target_system_arch += {'arc': arc_system_ss}
