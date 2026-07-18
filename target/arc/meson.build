gen = decodetree.process('insns.decode')
gen16 = decodetree.process('insns16.decode', extra_args: ['--static-decode=decode16', '--insnwidth=16'])
arc_ss = ss.source_set()
arc_ss.add(gen)
arc_ss.add(gen16)
arc_ss.add(files( 'cpu.c' , 'translate.c' , 'helper.c', 'disas.c'))
arc_system_ss = ss.source_set()
arc_system_ss.add(files())
target_arch += {'arc': arc_ss}
target_system_arch += {'arc': arc_system_ss}
