/* state.h - global shim state: rootfs, binds, virtual credentials
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
static char g_root[PM];
static size_t g_rl;
static char g_shim[PM];
#define LPM 65536
static char g_libpath[LPM];
static char g_loader[PM];
static char g_qemu[PM];
#define BENV 16384
static char g_bindenv[BENV];
static int g_noargv0;
/* comma-separated list of IPv4 and/or IPv6 literals */
static char g_dns[512] = "8.8.8.8,1.1.1.1";
static char g_self_exe[PM];
static int g_fake;
static int g_trace;
static u32 g_ruid, g_rgid;
/* virtual credentials for --no-root-id (host user, setuid/sudo emulation) */
static int g_user;
static char g_vuser[64];
static char g_credbuf[4096];
#define CR_NA ((u32)-1)
enum { C_RU, C_EU, C_SU, C_FU, C_RG, C_EG, C_SG, C_FG };
static u32 g_cr[8];
#define NVG 256
static u32 g_vg[NVG];
static int g_nvg;
static u32 g_pcr[8];
static int g_pcr_ok;
#define VROOT() (g_fake || (g_user && g_cr[C_EU] == 0))
static void cred_init(void);
static int cr_collect(const char*user, u32 gid, u32*out, int max);
#define MAXB 64
#define BPM 1024
static char g_bind[MAXB][BPM];
static size_t g_bindlen[MAXB];
static char g_bhost[MAXB][BPM];
static size_t g_bhlen[MAXB];
static int g_bid[MAXB];
static int g_nb;
