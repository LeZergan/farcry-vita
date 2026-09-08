/* Vita: stub, empty. x86 Model-Specific-Register access header, pulled in
   unconditionally by RenderDll/RenderPCH.h under #ifdef LINUX. Nothing in
   the renderer actually calls an MSR intrinsic from this header -- same
   category as engine_port/compat/sys/io.h (x86 raw I/O ports, meaningless
   on ARM). If a real symbol from here ever turns out to be needed, it
   means this LINUX branch never actually built upstream and needs a
   proper fix at the include site. */
