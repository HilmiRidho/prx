/* libc.h - libc imports, errno helpers (ret/fail/geterr), nextsym
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
extern int*__errno_location(void);
extern void*malloc(size_t);
extern void*realloc(void*, size_t);
extern void free(void*);
extern void*memcpy(void*, const void*, size_t);
extern void*memmove(void*, const void*, size_t);
extern void*memset(void*, int, size_t);
extern int memcmp(const void*, const void*, size_t);
extern size_t strlen(const char*);
extern int strncmp(const char*, const char*, size_t);
extern int strcmp(const char*, const char*);
extern char*getenv(const char*);
extern void*dlsym(void*, const char*);
extern char**environ;
extern int waitpid(int, int*, int);
extern int pipe2(int*, int);
extern void*fdopen(int, const char*);
extern int fclose(void*);
extern int fileno(void*);
extern void*signal(int, void*);
extern int socket(int, int, int);
extern int connect(int, const void*, unsigned);
extern int send(int, const void*, size_t, int);
extern long recv(int, void*, size_t, int);
extern int setsockopt(int, int, int, const void*, unsigned);
extern int posix_spawn_file_actions_init(void*);
extern int posix_spawn_file_actions_adddup2(void*, int, int);
extern int posix_spawn_file_actions_destroy(void*);
extern int posix_spawnattr_init(void*);
extern int posix_spawnattr_destroy(void*);
extern int posix_spawnattr_setsigdefault(void*, const void*);
extern int posix_spawnattr_setflags(void*, short);
extern int sigemptyset(void*);
extern int sigaddset(void*, int);
#define RTLD_NEXT ((void*)-1L)
static void*nextsym(const char*n) {
    return dlsym(RTLD_NEXT, n);
}
static long ret(long r) {
    if (r<0 && r>-4096) {
        *__errno_location() = (int)-r;
        return-1;
    }
    return r;
}
static long fail(int e) {
    *__errno_location() = e;
    return-1;
}
static int geterr(void) {
    return*__errno_location();
}
