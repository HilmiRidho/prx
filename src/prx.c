/* prx shim - LD_PRELOAD path-translation layer.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Built freestanding (-nostdlib) as ONE translation unit: the modules in
 * src/shim/ are textual fragments included below, in dependency order. Each
 * module may use everything defined by the ones above it, and tests include
 * this file directly to reach the static helpers. Add new code to the module
 * that owns the concern, or add a new module here at the right position. */
#include "shim/types.h"     /* integer types, flag/errno constants          */
#include "shim/sysnum.h"    /* per-arch syscall numbers, stat offsets       */
#include "shim/sys.h"       /* raw syscalls (sc), pread_, 64-bit div        */
#include "shim/libc.h"      /* libc imports, ret/fail/geterr, nextsym       */
#include "shim/state.h"     /* globals: rootfs, binds, virtual creds        */
#include "shim/init.h"      /* bind parsing, SIGSYS handler, init           */
#include "shim/path.h"      /* guest <-> host path translation              */
#include "shim/procfs.h"    /* fake /proc files, mountinfo                  */
#include "shim/fs_open.h"   /* open / fopen / dlopen                        */
#include "shim/fs_stat.h"   /* stat / statx family                          */
#include "shim/fs_ops.h"    /* access, cwd, rename, chmod, xattr, statfs... */
#include "shim/tty.h"       /* ioctl, isatty, tcgetattr, fcntl              */
#include "shim/glob_tmp.h"  /* glob, scandir, mkstemp                       */
#include "shim/net_unix.h"  /* AF_UNIX translation, sendmsg / recvmsg       */
#include "shim/proc.h"      /* capabilities, prctl, uname, signals, sysinfo */
#include "shim/cred.h"      /* setuid family, groups (virtual credentials)  */
#include "shim/pwdb.h"      /* passwd / group lookups, credential encoding  */
#include "shim/exec.h"      /* exec / spawn / system / popen                */
#include "shim/resolve.h"   /* IPv4/IPv6, /etc/hosts, DNS, getaddrinfo      */
