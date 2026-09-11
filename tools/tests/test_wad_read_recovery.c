#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include WSTD_SOURCE

FILE *M_fopen(const char *path, const char *mode) { return fopen(path, mode); }
long M_FileLength(FILE *f) { (void)f; abort(); }
char *M_StringDuplicate(const char *p) { return strdup(p); }
void *Z_Malloc(int n, int tag, void *user) { (void)tag; (void)user; return malloc(n); }
void Z_Free(void *p) { free(p); }

int i_pcm_active;
void I_PCM_DrainAsync(void) {}
static int seek_fails, read_fails, read_count;
static off64_t cursor;
static const char bytes[] = "abcdefghijklmnopqrstuvwxyz";
static ssize_t read_cookie(void *cookie, char *out, size_t count)
{
    (void)cookie; ++read_count;
    if (read_fails) { errno = EIO; return -1; }
    if (count > sizeof(bytes) - (size_t)cursor) count = sizeof(bytes) - (size_t)cursor;
    memcpy(out, bytes + cursor, count); cursor += count; return count;
}
static int seek_cookie(void *cookie, off64_t *offset, int whence)
{
    (void)cookie;
    if (seek_fails) { errno = EIO; return -1; }
    assert(whence == SEEK_SET && *offset >= 0 && *offset <= (off64_t)sizeof(bytes));
    cursor = *offset; return 0;
}
int main(void)
{
    cookie_io_functions_t io = { .read = read_cookie, .seek = seek_cookie };
    FILE *stream = fopencookie(NULL, "rb", io);
    assert(stream && setvbuf(stream, NULL, _IONBF, 0) == 0);
    stdc_wad_file_t wad = { .fstream = stream };
    char out[4];
    seek_fails = 1;
    assert(W_StdC_Read(&wad.wad, 8, out, 4) == 0 && read_count == 0);
    seek_fails = 0; read_fails = 1;
    assert(W_StdC_Read(&wad.wad, 8, out, 4) == 0 && ferror(stream));
    read_fails = 0;
    assert(W_StdC_Read(&wad.wad, 8, out, 4) == 4);
    assert(!ferror(stream) && !memcmp(out, bytes + 8, 4));
    fclose(stream);
    puts("PASS: failed WAD seek does not read the wrong offset; later I/O clears the error");
}
