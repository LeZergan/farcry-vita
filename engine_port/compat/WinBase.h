/* Compat shim: platform.h includes this unconditionally under LINUX for a
   resource-compiler macro that isn't exercised by the engine/game build.
   Left empty; if a real symbol turns out to be needed from here, it means
   the LINUX branch never actually built cleanly upstream and needs a
   proper fix at the include site instead of papering over it here. */
#pragma once
