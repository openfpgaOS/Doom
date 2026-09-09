/* Production directory parsing against truncated and out-of-range WADs. */
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include WAD_SOURCE

static jmp_buf failed;
static uint8_t data[64];
static unsigned available;
static wad_file_t file;
static void *allocations[8];
static int alloc_count;

void I_Error(const char *message, ...)
{ (void)message; longjmp(failed, 1); }
void *Z_Malloc(int size, int tag, void *user)
{
    (void)tag; (void)user;
    assert(size >= 0 && size <= 1024 && alloc_count < 8);
    void *p = calloc(1, size ? size : 1); assert(p);
    allocations[alloc_count++] = p; return p;
}
void Z_Free(void *p)
{
    for (int i = 0; i < alloc_count; ++i)
        if (allocations[i] == p) { free(p); allocations[i] = NULL; return; }
    abort();
}
void *I_Realloc(void *p, size_t size) { p = realloc(p, size); assert(p); return p; }
wad_file_t *W_OpenFile(const char *path) { (void)path; return &file; }
void W_CloseFile(wad_file_t *f) { assert(f == &file); }
size_t W_Read(wad_file_t *f, unsigned offset, void *buf, size_t size)
{
    assert(f == &file);
    if (offset >= available) return 0;
    if (size > available - offset) size = available - offset;
    memcpy(buf, data + offset, size); return size;
}
void M_ExtractFileBase(const char *path, char *dest)
{ (void)path; memcpy(dest, "SINGLE\0\0", 8); }

int main(int argc, char **argv)
{
    assert(argc == 2);
    wadinfo_t *h = (wadinfo_t *)data;
    filelump_t *d = (filelump_t *)(data + sizeof(*h));
    memcpy(h->identification, "IWAD", 4);
    h->numlumps = LONG(1); h->infotableofs = LONG(sizeof(*h));
    d->filepos = LONG(28); d->size = LONG(4); memcpy(d->name, "TESTDATA", 8);
    available = file.length = 32; file.mapped = data;
    int valid = !strcmp(argv[1], "valid");
    if (!strcmp(argv[1], "short-header")) available = file.length = 8;
    else if (!strcmp(argv[1], "short-directory")) available = 27;
    else if (!strcmp(argv[1], "directory-offset")) h->infotableofs = LONG(33);
    else if (!strcmp(argv[1], "directory-size")) h->numlumps = LONG(2);
    else if (!strcmp(argv[1], "directory-negative")) h->infotableofs = LONG(-1);
    else if (!strcmp(argv[1], "count-overflow")) h->numlumps = LONG(INT_MAX);
    else if (!strcmp(argv[1], "count-negative")) h->numlumps = LONG(-1);
    else if (!strcmp(argv[1], "lump-offset")) d->filepos = LONG(33);
    else if (!strcmp(argv[1], "lump-size")) d->size = LONG(5);
    else if (!strcmp(argv[1], "lump-negative")) d->filepos = LONG(-1);
    else if (!strcmp(argv[1], "size-negative")) d->size = LONG(-1);
    else if (!strcmp(argv[1], "empty-marker")) {
        d->filepos = LONG(-1); d->size = 0; valid = 1;
    } else assert(valid);
    if (setjmp(failed)) assert(!valid);
    else {
        assert(W_AddFile("test.wad") == &file);
        assert(valid && numlumps == 1 && !memcmp(lumpinfo[0]->name, "TESTDATA", 8));
        free(lumpinfo[0]); free(lumpinfo);
    }
    for (int i = 0; i < alloc_count; ++i) free(allocations[i]);
    printf("PASS: WAD %s\n", argv[1]);
}
