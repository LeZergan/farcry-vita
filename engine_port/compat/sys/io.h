/* Compat shim: x86 raw I/O port access (ioperm/inb/outb), meaningless on
   an ARM/Vita target. Stubbed empty; nothing in the game/engine code path
   should actually call these. */
#pragma once
