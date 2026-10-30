/* ipc_shm.h - System V shared memory emulation for kernels lacking CONFIG_SYSVIPC (Android)
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */

#define IPC_PRIVATE 0
#define IPC_RMID    0
#define IPC_SET     1
#define IPC_STAT    2
#define IPC_INFO    3

#define IPC_CREAT   01000
#define IPC_EXCL    02000

#define SHM_RDONLY  010000
#define SHM_RND     020000

#define PROT_READ_  0x1
#define PROT_WRITE_ 0x2
#define MAP_SHARED_ 0x01
#define MAP_FIXED_  0x10

struct prx_shm_meta {
    u32 key;
    u32 shmid;
    u64 size;
    u32 mode;
    u32 deleted;
    u32 nattch;
    u32 creator_pid;
};

#define MAX_LOCAL_SHM 128
struct prx_local_shm {
    void *addr;
    size_t size;
    int shmid;
};
static struct prx_local_shm g_shm_att[MAX_LOCAL_SHM];

/* Helper: Create a path in the guest's rootfs /dev/shm */
static int shm_build_path(char *out, const char *type, int id) {
    if (!g_rl) return -1;
    char num[16];
    int n = 0;
    unsigned int v = (id < 0) ? (unsigned int)-id : (unsigned int)id;
    do {
        num[n++] = '0' + (v % 10);
        v /= 10;
    } while (v);
    
    /* Target: $PRX_ROOTFS/dev/shm/.prx_shm_<type>_<num> */
    size_t k = 0;
    memcpy(out + k, g_root, g_rl);
    k += g_rl;
    const char sub[] = "/dev/shm/.prx_shm_";
    memcpy(out + k, sub, sizeof(sub) - 1);
    k += sizeof(sub) - 1;
    
    while (*type) out[k++] = *type++;
    out[k++] = '_';
    while (n) out[k++] = num[--n];
    out[k] = 0;
    return 0;
}

/* Read segment metadata */
static int shm_read_meta(int shmid, struct prx_shm_meta *meta) {
    char p[PM];
    if (shm_build_path(p, "m", shmid) < 0) return -1;
    long fd = SC4(N_openat, AT_FDCWD, p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return -1;
    long r = SC3(N_read, fd, meta, sizeof(*meta));
    sc(N_close, fd, 0, 0, 0, 0, 0);
    return (r == sizeof(*meta)) ? 0 : -1;
}

/* Write segment metadata */
static int shm_write_meta(int shmid, const struct prx_shm_meta *meta) {
    char p[PM];
    if (shm_build_path(p, "m", shmid) < 0) return -1;
    long fd = SC4(N_openat, AT_FDCWD, p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    long w = SC3(N_write, fd, meta, sizeof(*meta));
    sc(N_close, fd, 0, 0, 0, 0, 0);
    return (w == sizeof(*meta)) ? 0 : -1;
}

static void shm_unlink_all(int shmid, u32 key) {
    char p[PM];
    if (shm_build_path(p, "d", shmid) == 0) SC3(N_unlinkat, AT_FDCWD, p, 0);
    if (shm_build_path(p, "m", shmid) == 0) SC3(N_unlinkat, AT_FDCWD, p, 0);
    if (key != IPC_PRIVATE && shm_build_path(p, "k", (int)key) == 0) SC3(N_unlinkat, AT_FDCWD, p, 0);
}

EXP int shmget(int key, size_t size, int shmflg) {
    static int (*real_shmget)(int, size_t, int);
    if (!real_shmget) real_shmget = nextsym("shmget");
  
    /* 1. Try the native kernel SYSV IPC first */
    if (real_shmget) {
        int r = real_shmget(key, size, shmflg);
        if (r >= 0) return r;
        int err = geterr();
        if (err != ENOSYS && err != EACCES && err != EPERM) return -1;
    }

    /* 2. Fallback emulation using a backing file */
    if (size > 0x7fffffffULL) return (int)fail(EINVAL);
    
    int shmid = -1;
    char kp[PM], dp[PM];

    /* If the key is not IPC_PRIVATE, check whether the key already has an associated shmid */
    if (key != IPC_PRIVATE) {
        if (shm_build_path(kp, "k", key) < 0) return (int)fail(ENOMEM);
        long kfd = SC4(N_openat, AT_FDCWD, kp, O_RDONLY | O_CLOEXEC, 0);
        if (kfd >= 0) {
            u32 old_id = 0;
            long rd = SC3(N_read, kfd, &old_id, sizeof(old_id));
            sc(N_close, kfd, 0, 0, 0, 0, 0);
            if (rd == sizeof(old_id)) {
                if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) return (int)fail(EEXIST);
                struct prx_shm_meta meta;
                if (shm_read_meta(old_id, &meta) == 0) {
                    if (size > meta.size) return (int)fail(EINVAL);
                    return old_id;
                }
            }
        }
        if (!(shmflg & IPC_CREAT)) return (int)fail(ENOENT);
    }

    if (size == 0) return (int)fail(EINVAL);

    /* Search for unused random IDs (1 .. 65535) */
    u16 rnd = 0;
    for (int tries = 0; tries < 1000; tries++) {
        sc(N_getrandom, (long)&rnd, sizeof(rnd), 0, 0, 0, 0);
        int cand = (rnd & 0x7fff) + 1;
        if (shm_build_path(dp, "d", cand) < 0) return (int)fail(ENOMEM);
        long dfd = SC4(N_openat, AT_FDCWD, dp, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (dfd >= 0) {
            /* Data file successfully created */
            if (SC3(N_truncate, dp, size, 0) < 0) {
                sc(N_close, dfd, 0, 0, 0, 0, 0);
                SC3(N_unlinkat, AT_FDCWD, dp, 0);
                return (int)fail(ENOMEM);
            }
            sc(N_close, dfd, 0, 0, 0, 0, 0);
            shmid = cand;
            break;
        }
    }
    if (shmid < 0) return (int)fail(ENOSPC);

    /* Initialize metadata */
    struct prx_shm_meta meta;
    memset(&meta, 0, sizeof(meta));
    meta.key = (u32)key;
    meta.shmid = (u32)shmid;
    meta.size = size;
    meta.mode = shmflg & 0777;
    meta.deleted = 0;
    meta.nattch = 0;
    meta.creator_pid = (u32)sc(N_getpid, 0, 0, 0, 0, 0, 0);

    if (shm_write_meta(shmid, &meta) < 0) {
        SC3(N_unlinkat, AT_FDCWD, dp, 0);
        return (int)fail(ENOMEM);
    }

    /* Save index key -> shmid */
    if (key != IPC_PRIVATE) {
        long kfd = SC4(N_openat, AT_FDCWD, kp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (kfd >= 0) {
            u32 sid = (u32)shmid;
            SC3(N_write, kfd, &sid, sizeof(sid));
            sc(N_close, kfd, 0, 0, 0, 0, 0);
        }
    }

    return shmid;
}

EXP void *shmat(int shmid, const void *shmaddr, int shmflg) {
    static void *(*real_shmat)(int, const void*, int);
    static void *(*real_mmap)(void*, size_t, int, int, int, long);
    
    struct prx_shm_meta meta;
    if (shm_read_meta(shmid, &meta) < 0) {
        /* If not present in the emulation metadata, try invoking the native syscall */
        if (!real_shmat) real_shmat = nextsym("shmat");
        if (real_shmat) return real_shmat(shmid, shmaddr, shmflg);
        fail(EINVAL);
        return (void*)-1;
    }

    char dp[PM];
    if (shm_build_path(dp, "d", shmid) < 0) {
        fail(ENOMEM);
        return (void*)-1;
    }

    long fd = SC4(N_openat, AT_FDCWD, dp, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) {
        fail(EINVAL);
        return (void*)-1;
    }

    if (!real_mmap) real_mmap = nextsym("mmap");
    if (!real_mmap) {
        sc(N_close, fd, 0, 0, 0, 0, 0);
        fail(ENOSYS);
        return (void*)-1;
    }

    int prot = PROT_READ_ | ((shmflg & SHM_RDONLY) ? 0 : PROT_WRITE_);
    int flags = MAP_SHARED_ | (shmaddr ? MAP_FIXED_ : 0);

    void *addr = real_mmap((void*)shmaddr, (size_t)meta.size, prot, flags, (int)fd, 0);
    sc(N_close, fd, 0, 0, 0, 0, 0);

    if (addr == (void*)-1 || (long)addr < 0) {
        fail(ENOMEM);
        return (void*)-1;
    }

    /* Record to the local attachment table */
    for (int i = 0; i < MAX_LOCAL_SHM; i++) {
        if (!g_shm_att[i].addr) {
            g_shm_att[i].addr = addr;
            g_shm_att[i].size = (size_t)meta.size;
            g_shm_att[i].shmid = shmid;
            break;
        }
    }

    meta.nattch++;
    shm_write_meta(shmid, &meta);

    return addr;
}

EXP int shmdt(const void *shmaddr) {
    static int (*real_shmdt)(const void*);
    static int (*real_munmap)(void*, size_t);

    int idx = -1;
    for (int i = 0; i < MAX_LOCAL_SHM; i++) {
        if (g_shm_att[i].addr == shmaddr) {
            idx = i;
            break;
        }
    }

    /* If not registered in the emulation, pass through to the native shmdt */
    if (idx < 0) {
        if (!real_shmdt) real_shmdt = nextsym("shmdt");
        return real_shmdt ? real_shmdt(shmaddr) : (int)fail(EINVAL);
    }

    if (!real_munmap) real_munmap = nextsym("munmap");
    if (real_munmap) real_munmap((void*)shmaddr, g_shm_att[idx].size);

    int shmid = g_shm_att[idx].shmid;
    g_shm_att[idx].addr = NULL;

    struct prx_shm_meta meta;
    if (shm_read_meta(shmid, &meta) == 0) {
        if (meta.nattch > 0) meta.nattch--;
        if (meta.deleted && meta.nattch == 0) {
            shm_unlink_all(shmid, meta.key);
        } else {
            shm_write_meta(shmid, &meta);
        }
    }

    return 0;
}

EXP int shmctl(int shmid, int cmd, void *buf) {
    static int (*real_shmctl)(int, int, void*);
    struct prx_shm_meta meta;
    
    if (shm_read_meta(shmid, &meta) < 0) {
        if (!real_shmctl) real_shmctl = nextsym("shmctl");
        return real_shmctl ? real_shmctl(shmid, cmd, buf) : (int)fail(EINVAL);
    }

    if (cmd == IPC_RMID) {
        meta.deleted = 1;
        if (meta.key != IPC_PRIVATE) {
            char kp[PM];
            if (shm_build_path(kp, "k", (int)meta.key) == 0) SC3(N_unlinkat, AT_FDCWD, kp, 0);
        }
        if (meta.nattch == 0) {
            shm_unlink_all(shmid, meta.key);
        } else {
            shm_write_meta(shmid, &meta);
        }
        return 0;
    }

    if (cmd == IPC_STAT && buf) {
        memset(buf, 0, 112);
        /* Offset shm_segsz & shm_nattch on glibc 64-bit / 32-bit */
#if defined(__i386__)
        *(u32*)((char*)buf + 24) = (u32)meta.size;
        *(u32*)((char*)buf + 40) = meta.nattch;
        *(u16*)((char*)buf + 14) = (u16)meta.mode;
#else
        *(u64*)((char*)buf + 48) = meta.size;
        *(u64*)((char*)buf + 88) = (u64)meta.nattch;
        *(u16*)((char*)buf + 20) = (u16)meta.mode;
#endif
        return 0;
    }

    if (cmd == IPC_SET && buf) {
#if defined(__i386__)
        meta.mode = *(u16*)((char*)buf + 14) & 0777;
#else
        meta.mode = *(u16*)((char*)buf + 20) & 0777;
#endif
        shm_write_meta(shmid, &meta);
        return 0;
    }

    return 0;
}
