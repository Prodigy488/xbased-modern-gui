# xbased's core on Linux x86-64 (clang): the guest CPU is cpu/cpu_x86.cpp
# as on Windows (Zydis), the few Windows APIs it uses come from
# compat/linux (nc_winapi.h, seh.c: vectored handlers over SIGSEGV), and
# what Android's libc has and glibc hasn't (system properties) from
# compat/linux too. SDL3 is built here for the APU's output and the front end.
set(NC_DEPS "${CMAKE_CURRENT_LIST_DIR}/../../../deps" CACHE PATH "native core third-party sources")

set(ZYDIS_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_MAN OFF CACHE BOOL "" FORCE)
set(ZYCORE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
add_subdirectory("${NC_DEPS}/zydis" zydis-build EXCLUDE_FROM_ALL)
add_library(nc_zydis INTERFACE)
target_link_libraries(nc_zydis INTERFACE Zydis)

set(NC_XXHASH_DIR "${NC_DEPS}/xxhash")

# The guest's 4 GiB live at host address 0 (core/vm.c): pointer arithmetic on
# that null base is everyday business, no optimization may assume otherwise.
# A PIE executable: it and its libraries load far above the guest's space.
add_compile_options(-fno-delete-null-pointer-checks -msse4.1 -mpopcnt -g1 -fPIE)
add_compile_definitions(_GNU_SOURCE)
add_link_options(-pie)
# C++'s runtime inside: the binary asks the system for glibc (and zlib) only
add_link_options(-static-libstdc++ -static-libgcc)
set(NC_LINUX_COMPAT_INC "${CMAKE_CURRENT_LIST_DIR}/../compat/linux/include")
function(nc_linux_compat)
  foreach(t ${ARGN})
    target_include_directories(${t} BEFORE PRIVATE "${NC_LINUX_COMPAT_INC}")
  endforeach()
endfunction()

# both PC platforms' code paths (SDL audio, the props shim, ...): NC_DESKTOP
add_compile_definitions(NC_DESKTOP=1)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
add_subdirectory("${NC_DEPS}/SDL3" sdl3 EXCLUDE_FROM_ALL)
