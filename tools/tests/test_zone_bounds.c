/* Invalid allocation sizes must not corrupt the zone's block chain. */
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include ZONE_SOURCE

static byte pool[65536] __attribute__((aligned(8)));
static jmp_buf failed;
byte *I_ZoneBase(int *size) { *size = sizeof(pool); return pool; }
int I_CheckZoneCanaries(void) { return 1; }
void I_Error(const char *message, ...) { (void)message; longjmp(failed, 1); }
boolean W_PointerInWadMapped(const void *p, char *name, unsigned int *offset)
{ (void)p; (void)name; (void)offset; return false; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    Z_Init();
    if (!strcmp(argv[1], "valid")) {
        for (int i = 0; i < 1024; ++i) {
            void *p = Z_Malloc(i, PU_STATIC, NULL); assert(p);
            memset(p, 0x5a, i); Z_Free(p); Z_CheckHeap();
        }
    } else {
        int size = !strcmp(argv[1], "negative") ? -64 : INT_MAX;
        if (setjmp(failed)) Z_CheckHeap();
        else { (void)Z_Malloc(size, PU_STATIC, NULL); assert(0); }
        void *p = Z_Malloc(100, PU_STATIC, NULL); Z_Free(p); Z_CheckHeap();
    }
    puts("PASS: zone allocation bounds and subsequent heap integrity");
}
