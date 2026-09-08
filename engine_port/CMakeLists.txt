cmake_minimum_required(VERSION 3.16)

# Vita: real build for the ported engine (CrySystem/Cry3DEngine/etc + the
# from-scratch CVitaRenderer). Source list in vita_sources.txt is not a guess
# -- it's reconstructed from the .o files actually left behind by the last
# verified-compiling state of this tree (see git log's per-module "N files
# compile clean" commits), mapped back to their real locations in the
# original Crytek source tree. Compile flags match engine_port/compat/README.md.

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(farcry_vita C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Deterministic Vita3K smoke-test mode.  The normal build remains interactive;
# CI/local bring-up builds can enter the real Training level without relying on
# host keyboard injection into the guest.
option(FARCRY_VITA_AUTOTEST_TRAINING "Automatically load the Training level in Vita3K" OFF)
option(FARCRY_VITA_AUTOTEST_MENU "Drive one deterministic retail-menu confirmation in Vita3K" OFF)
option(FARCRY_VITA_FULL_GAME "Link the original CryGame implementation into the Vita executable" ON)
# Traces every static-object physics mesh as it is registered with CryPhysics.
# Scoped to StatObjPhys.cpp alone so toggling it is a one-file rebuild.
option(FARCRY_VITA_DEBUG_PHYSICALIZE "Trace CStatObj::Physicalize mesh registration" OFF)
# Dumps mixer peak level and sample-parse tallies to ux0:data/farcry_audio.txt.
option(FARCRY_VITA_DEBUG_AUDIO "Trace the CrySound mixer's output level" OFF)
# Periodically logs the outdoor/indoor clear colour and the sky render element.
option(FARCRY_VITA_DEBUG_CLEARCOLOR "Trace C3DEngine::SetupClearColor" OFF)
# Collects subsystem/draw/upload timing and writes it to the game log every
# 120 frames.  Keep this opt-in: process-timer reads in the hot path and
# periodic memory-card writes are useful for a profiling build but can create
# the very frame-time spikes a release build is supposed to avoid.
option(FARCRY_VITA_PERF_TELEMETRY "Enable periodic Vita performance telemetry" OFF)
# Internal render resolution as a percentage of the Vita's 960x544 panel; the
# hardware scaler brings it back up.  100 is native, 75 and 66 are the useful
# steps if the SGX543 turns out to be fill-rate bound on real hardware.
set(FARCRY_VITA_RENDER_SCALE "100" CACHE STRING "Internal render resolution, percent of native")

set(VITA_APP_NAME "Far Cry")
set(VITA_TITLEID  "FCRY00002")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

set(FARCRY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Vita: CryCompat.h must be force-included ahead of EVERYTHING in every
# translation unit (see its own comment at engine_port/compat/CryCompat.h) --
# real Crytek headers (icrypak.h, meshidx.h, etc) reference Win32 types like
# FILETIME with no LINUX-branch definition of their own; CryCompat.h supplies
# them, but only if it's visible before those headers are first parsed.
set(FORCE_INCLUDE_FLAG "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/CryCompat.h")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLINUX -DLINUX32 -mtune=cortex-a9 -std=gnu++11 -fpermissive -Wno-narrowing -Wl,-q ${FORCE_INCLUDE_FLAG}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLINUX -DLINUX32 -mtune=cortex-a9 -Wl,-q")

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/compat
  ${FARCRY_ROOT}/third_party/libbinkdec/include
  ${FARCRY_ROOT}/CryCommon
  ${FARCRY_ROOT}/CrySystem
  ${FARCRY_ROOT}/CrySystem/zlib
  ${FARCRY_ROOT}/CrySystem/XML
  ${FARCRY_ROOT}/CrySystem/XML/Expat
  ${FARCRY_ROOT}/Cry3DEngine
  ${FARCRY_ROOT}/CryAnimation
  ${FARCRY_ROOT}/CryPhysics
  ${FARCRY_ROOT}/CryNetwork
  ${FARCRY_ROOT}/CryEntitySystem
  ${FARCRY_ROOT}/CryFont
  ${FARCRY_ROOT}/CryFont/FreeType2/include
  ${FARCRY_ROOT}/CryScriptSystem
  ${FARCRY_ROOT}/CryScriptSystem/LUA
  ${FARCRY_ROOT}/CrySoundSystem
  ${FARCRY_ROOT}/CryAISystem
  ${FARCRY_ROOT}/CryMovie
  ${FARCRY_ROOT}/CryInput
  ${FARCRY_ROOT}/CryGame
  ${FARCRY_ROOT}/PunkBuster
  ${FARCRY_ROOT}/RenderDll
  ${FARCRY_ROOT}/RenderDll/Common
  ${FARCRY_ROOT}/RenderDll/XRenderNULL
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# vita_sources.txt: one repo-relative path per line, resolved from the known-
# good .o inventory (see git history of this file's generation).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/vita_sources.txt" VITA_SOURCE_LIST)
set(VITA_SOURCES "")
foreach(_src ${VITA_SOURCE_LIST})
  list(APPEND VITA_SOURCES "${FARCRY_ROOT}/${_src}")
endforeach()

# Far Cry's single-player game still uses CryNetwork's in-process server and
# local-client transport for all entity replication.  The earlier Vita bring-up
# substituted NetworkMissing.cpp, whose CreateServer/CreateClient methods
# unconditionally returned NULL; that made every real level load fail before it
# reached the 3D engine.  Use Crytek's original local transport implementation.
list(REMOVE_ITEM VITA_SOURCES "${FARCRY_ROOT}/CryNetwork/NetworkMissing.cpp")
list(APPEND VITA_SOURCES
  "${FARCRY_ROOT}/CryNetwork/Network.cpp"
  "${FARCRY_ROOT}/CryNetwork/Client.cpp"
  "${FARCRY_ROOT}/CryNetwork/ClientLocal.cpp"
  "${FARCRY_ROOT}/CryNetwork/Server.cpp"
  "${FARCRY_ROOT}/CryNetwork/ServerSlot.cpp")

list(APPEND VITA_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/VitaInput.cpp"
  "${FARCRY_ROOT}/CryInput/XActionMap.cpp"
  "${FARCRY_ROOT}/CryInput/XActionMapManager.cpp")

# CrySound is proprietary middleware with no source in this tree.  The earlier
# bring-up satisfied the linker with CrySoundStubs.cpp, whose every entry point
# returns 0 -- that is what left the port completely silent.  CrySoundVita.cpp
# is a real sceAudioOut mixer implementing the same interface; the stub file
# stays in the tree as the reference for what the full surface looks like.
list(REMOVE_ITEM VITA_SOURCES "${FARCRY_ROOT}/CrySoundSystem/CrySoundStubs.cpp")
list(APPEND VITA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CrySoundVita.cpp")

# Bink video.  The retail cut scenes and demo reels are .bik, a proprietary RAD
# format with no decoder in this tree and none in the Vita's hardware decoder
# (which does H.264).  libbinkdec is a standalone LGPL-2.1 decoder derived from
# FFmpeg's; vendored under third_party/ rather than reimplementing a codec.
set(LIBBINKDEC_DIR "${FARCRY_ROOT}/third_party/libbinkdec")
file(GLOB LIBBINKDEC_SOURCES "${LIBBINKDEC_DIR}/src/*.cpp" "${LIBBINKDEC_DIR}/src/*.c")
list(APPEND VITA_SOURCES ${LIBBINKDEC_SOURCES})
# The decoder throws nothing and needs no unwinding; keeping its exception and
# unwind tables out holds .ARM.exidx down, which vita-elf-create is sensitive to.
set_source_files_properties(${LIBBINKDEC_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-fno-exceptions;-fno-unwind-tables;-fno-asynchronous-unwind-tables;-marm;-ffast-math;-fomit-frame-pointer")

# The retail executable loaded CryGame as a DLL on desktop.  Vita has no
# useful DLL boundary here, so compile the original game module directly and
# let CSystem::CreateGame call its normal CreateGameInstance entry point.
if(FARCRY_VITA_FULL_GAME)
  file(GLOB CRYGAME_SOURCES CONFIGURE_DEPENDS "${FARCRY_ROOT}/CryGame/*.cpp")
  # The DivX implementation is a Windows/DirectSound-only optional fallback.
  list(REMOVE_ITEM CRYGAME_SOURCES "${FARCRY_ROOT}/CryGame/UIDivX_Video.cpp")
  list(APPEND VITA_SOURCES ${CRYGAME_SOURCES})
endif()

add_executable(${PROJECT_NAME} ${VITA_SOURCES})

# These retail PC middleware paths are not used by the in-process single-player
# transport and have no Vita SDK.  Compiling them out also saves code and heap.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  NOT_USE_UBICOM_SDK=1
  NOT_USE_PUNKBUSTER_SDK=1
  NOT_USE_ASE_SDK=1)

if(FARCRY_VITA_AUTOTEST_TRAINING)
  target_compile_definitions(${PROJECT_NAME} PRIVATE VITA_DEBUG_AUTOLOAD_TRAINING=1)
endif()
if(FARCRY_VITA_AUTOTEST_MENU)
  target_compile_definitions(${PROJECT_NAME} PRIVATE VITA_DEBUG_AUTOTEST_MENU=1)
endif()
if(FARCRY_VITA_FULL_GAME)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    FARCRY_VITA_FULL_GAME=1
    CRYGAME_EXPORTS=1)
endif()

if(FARCRY_VITA_DEBUG_PHYSICALIZE)
  set_source_files_properties(${FARCRY_ROOT}/Cry3DEngine/StatObjPhys.cpp
    PROPERTIES COMPILE_DEFINITIONS "VITA_DEBUG_PHYSICALIZE=1")
endif()
if(FARCRY_VITA_DEBUG_AUDIO)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/CrySoundVita.cpp
    PROPERTIES COMPILE_DEFINITIONS "VITA_DEBUG_AUDIO=1")
endif()
if(FARCRY_VITA_DEBUG_CLEARCOLOR)
  set_source_files_properties(${FARCRY_ROOT}/Cry3DEngine/3DEngineRender.cpp
    ${FARCRY_ROOT}/RenderDll/XRenderNULL/VitaRenderer.cpp
    PROPERTIES COMPILE_DEFINITIONS "VITA_DEBUG_CLEARCOLOR=1")
endif()
if(FARCRY_VITA_PERF_TELEMETRY)
  set_source_files_properties(${FARCRY_ROOT}/CrySystem/System.cpp
    ${FARCRY_ROOT}/RenderDll/XRenderNULL/VitaRenderer.cpp
    PROPERTIES COMPILE_DEFINITIONS "VITA_PERF_TELEMETRY=1")
endif()
if(NOT FARCRY_VITA_RENDER_SCALE STREQUAL "100")
  set_property(SOURCE ${FARCRY_ROOT}/RenderDll/XRenderNULL/VitaRenderer.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS
      "FARCRY_VITA_RENDER_SCALE_PERCENT=${FARCRY_VITA_RENDER_SCALE}")
endif()

set_source_files_properties(
  ${FARCRY_ROOT}/CryFont/FreeType2/src/base/ftbase.c
  ${FARCRY_ROOT}/CryFont/FreeType2/src/base/ftsystem.c
  ${FARCRY_ROOT}/CryFont/FreeType2/src/base/ftinit.c
  ${FARCRY_ROOT}/CryFont/FreeType2/src/base/ftglyph.c
  ${FARCRY_ROOT}/CryFont/FreeType2/src/base/ftdebug.c
  PROPERTIES COMPILE_DEFINITIONS "FT2_BUILD_LIBRARY"
)

# Vita: CryScriptSystem/LUA/lua.h:99-111 requires LUA_API to be `extern "C"`
# under LINUX -- real Crytek code, not a shim -- which is only valid inside a
# C++ translation unit. Force the whole LUA/ directory to compile as C++.
foreach(_luasrc ${VITA_SOURCE_LIST})
  if(_luasrc MATCHES "^CryScriptSystem/LUA/.*\\.c$" OR
     _luasrc STREQUAL "CryScriptSystem/vectorlib.c")
    set_source_files_properties(${FARCRY_ROOT}/${_luasrc} PROPERTIES LANGUAGE CXX)
  endif()
endforeach()

# The SDK's stock vitaGL archive is commonly built without
# ENABLE_LEGACY_PIPELINE: it still exports vglDrawObjects, but as a two-byte
# no-op.  Far Cry's static-world fast path requires the real upstream
# implementation, so the Vita kit carries the official df63ce8 build made with
# ENABLE_LEGACY_PIPELINE=1.  Keep the SDK library as a developer fallback.
set(FARCRY_VITAGL_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/vita_kit/libvitaGL_farcry.a")
if(NOT EXISTS "${FARCRY_VITAGL_LIBRARY}")
  set(FARCRY_VITAGL_LIBRARY vitaGL)
endif()

target_link_libraries(${PROJECT_NAME}
  ${FARCRY_VITAGL_LIBRARY}
  stdc++
  vitashark
  SceShaccCgExt
  SceShaccCg_stub
  mathneon
  kubridge_stub
  taihen_stub
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceMotion_stub
  SceTouch_stub
  SceHid_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceCommonDialog_stub
  ScePower_stub
  SceKernelDmacMgr_stub
  SceSysmem_stub
  SceSysmodule_stub
  SceNet_stub
  SceNetCtl_stub
  SceAudio_stub
  SceAudioIn_stub
	SceAvPlayer_stub
  SceRtc_stub
  SceProcessmgr_stub
  SceIofilemgr_stub
  m
  z
  pthread
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
