/* prx.c - LD_PRELOAD path-translation shim
 *
 * The shim is compiled from this file alone, with -nostdlib -ffreestanding, as
 * one translation unit. Its code is split into the prx_*.h headers below, which
 * are included in dependency order: later parts use what earlier ones define,
 * so the order matters. `prx source` prints this file with the includes expanded.
 *
 * Where things live:
 *   prx_sys.h   freestanding runtime for the shim
 *   prx_core.h  shim state, bind table and start-up
 *   prx_path.h  guest <-> host path translation
 *   prx_fs.h    interposed filesystem entry points
 *   prx_net.h   interposed socket entry points
 *   prx_misc.h  capabilities, prctl, uname, signals and other small hooks
 *   prx_cred.h  fake root and virtual credentials
 *   prx_exec.h  exec, spawn and library-path handling
 *   prx_dns.h   name resolution without the guest resolver
 */
#include "prx_sys.h"
#include "prx_core.h"
#include "prx_path.h"
#include "prx_fs.h"
#include "prx_net.h"
#include "prx_misc.h"
#include "prx_cred.h"
#include "prx_exec.h"
#include "prx_dns.h"
