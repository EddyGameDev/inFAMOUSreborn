/*
 * ps3recomp - the thread-local storage qualifier, on its own.
 *
 * PPU_THREAD_LOCAL is defined in three places that must agree: here,
 * ppu_context.h, and the lifter's HEADER_PREAMBLE (tools/ppu_lifter.py) which
 * bakes it into every generated ppu_recomp.h. The #ifndef makes including any
 * combination harmless.
 *
 * It lives in its own header because the boot scaffold and the loader need the
 * QUALIFIER but must not pull in a second definition of `ppu_context`: a port
 * built from a generated ppu_recomp.h already declares that struct, and
 * including ppu_context.h there is a redefinition error. Ports generated before
 * the qualifier existed do not define it at all, so the scaffold has to carry
 * its own reachable definition or it fails to build against exactly those
 * (older, not yet re-lifted) titles.
 */
#ifndef PS3RECOMP_PPU_TLS_H
#define PS3RECOMP_PPU_TLS_H

#ifndef PPU_THREAD_LOCAL
#  if defined(_MSC_VER) && defined(__clang__) && defined(__cplusplus)
     /* v181: clang-cl (MSVC >= 19.25 compat) puts an on-demand TLS-init guard (__tls_guard test + call) in front of
      * EVERY access to an extern thread-local -- ~200k sites in the lifted PPU code (the g_trampoline_fn drain after
      * each call). require_constant_initialization tells it the object needs no dynamic init (clang ENFORCES that
      * at every definition: a non-constant initializer is a compile error), so the guard goes. C ignores it. */
#    define PPU_THREAD_LOCAL __attribute__((require_constant_initialization)) __declspec(thread)
#  elif defined(_MSC_VER)
#    define PPU_THREAD_LOCAL __declspec(thread)
#  else
#    define PPU_THREAD_LOCAL __thread
#  endif
#endif

#endif /* PS3RECOMP_PPU_TLS_H */
