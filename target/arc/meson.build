gen = decodetree.process('insns.decode')
arc_ss = ss.source_set()
arc_ss.add(gen)
arc_ss.add(files( 'cpu.c' , 'translate.c'))
arc_system_ss = ss.source_set()
arc_system_ss.add(files())

target_arch += {'arc': arc_ss}
target_system_arch += {'arc': arc_system_ss}
