//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//      Zone Memory Allocation. Classic first-fit pool allocator with
//      a rover pointer and tag-based purging — the same design Doom
//      shipped with. We use this instead of z_native.c on openfpgaOS
//      because musl's heap fragments across level/demo transitions:
//      after one demo loop the heap has plenty of free bytes but no
//      contiguous 41 KB slot, so Z_Malloc fails. A dedicated pool
//      never gives memory back to libc, so fragmentation is contained
//      and purgable blocks can be freed to satisfy allocations.
//

#include <stdint.h>
#include <string.h>

#include "z_zone.h"
#include "i_system.h"
#include "doomtype.h"
#include "w_wad.h"

#define ZONEID 0x1d4a11
#define MINFRAGMENT 64
#define ZONE_ALIGN 8
#define ZONE_ALIGN_MASK (ZONE_ALIGN - 1)

typedef struct memblock_s {
    int                size;    // including header
    void             **user;
    int                tag;
    int                id;      // ZONEID
    struct memblock_s *next;
    struct memblock_s *prev;
} memblock_t;

typedef struct {
    int         size;           // total bytes in zone
    memblock_t  blocklist;      // sentinel (head of circular list)
    memblock_t *rover;
} memzone_t;

static memzone_t *mainzone;

typedef char z_memblock_size_must_be_aligned[(sizeof(memblock_t) & ZONE_ALIGN_MASK) == 0 ? 1 : -1];
typedef char z_memzone_size_must_be_aligned[(sizeof(memzone_t) & ZONE_ALIGN_MASK) == 0 ? 1 : -1];

static int Z_IsAligned(const void *ptr)
{
    return (((uintptr_t) ptr) & ZONE_ALIGN_MASK) == 0;
}

static void Z_RequireAligned(const void *ptr, const char *what)
{
    if (!Z_IsAligned(ptr))
        I_Error("%s: misaligned address %p", what, ptr);
}

static memzone_t *Z_MemzoneAt(void *ptr, const char *what)
{
    Z_RequireAligned(ptr, what);
    return (memzone_t *)ptr;
}

static memblock_t *Z_MemblockAt(void *ptr, const char *what)
{
    Z_RequireAligned(ptr, what);
    return (memblock_t *)ptr;
}

void Z_Init(void)
{
    memblock_t *block;
    int        size;

    mainzone       = Z_MemzoneAt(I_ZoneBase(&size), "Z_Init: zone base");
    mainzone->size = size;

    // Set the entire zone to one free block.
    mainzone->blocklist.next = block =
        Z_MemblockAt((byte *)mainzone + sizeof(memzone_t), "Z_Init: first block");
    mainzone->blocklist.prev = block;
    mainzone->blocklist.user = (void **)mainzone;
    mainzone->blocklist.tag  = PU_STATIC;
    mainzone->rover          = block;

    block->prev = block->next = &mainzone->blocklist;
    block->user = NULL;         // NULL user marks a free block
    block->size = mainzone->size - sizeof(memzone_t);
    block->tag  = PU_FREE;
    block->id   = 0;

    printf("zone memory: %p, %x allocated\n", mainzone, mainzone->size);
}

// Classify what went wrong with a Z_Free(ptr) call whose block->id != ZONEID.
// Returns a short string verdict and (if applicable) fills in *matched_tag
// with the tag we found the block carrying inside the block chain.
//
// Walks the full circular block list bounded by a step limit so a corrupt
// prev/next link can't spin us forever.
static const char *Z_FreeFailureClass(const memblock_t *block, int *matched_tag)
{
    const memblock_t *b;
    int steps = 0;
    const int max_steps = 1 << 20;  // hard cap; zone has far fewer blocks

    for (b = mainzone->blocklist.next;
         b != &mainzone->blocklist && steps < max_steps;
         b = b->next, ++steps)
    {
        if (b == block)
        {
            if (matched_tag) *matched_tag = b->tag;
            if (b->tag == PU_FREE) return "DOUBLE-FREE (block already in free list)";
            return "header scribbled (block is in chain but id != ZONEID)";
        }
    }
    if (steps >= max_steps) return "chain walk aborted (prev/next corrupted?)";
    return "stray pointer (block not in any zone chain)";
}

static void Z_DumpFreeFailure(const void *ptr, const memblock_t *block,
                              const void *caller_pc)
{
    const byte *p      = (const byte *)ptr;
    const byte *pool_lo = (const byte *)mainzone;
    const byte *pool_hi = pool_lo + mainzone->size;
    char  lump_name[8];
    unsigned int lump_off = 0;
    const byte *hex_base;
    const char *verdict;
    int    matched_tag = -1;
    int    i;

    printf("Z_Free FAIL: ptr=%p block_hdr=%p id=%08x tag=%d size=%d\n",
           ptr, (const void *)block, block->id, block->tag, block->size);
    printf("  caller PC=%p  (decode: riscv64-elf-addr2line -e app.elf -fi <PC>)\n",
           caller_pc);

    verdict = Z_FreeFailureClass(block, &matched_tag);
    if (matched_tag >= 0)
        printf("  verdict: %s (matched_tag=%d)\n", verdict, matched_tag);
    else
        printf("  verdict: %s\n", verdict);

    if (p >= pool_lo && p < pool_hi)
        printf("  in zone pool [+0x%x / size 0x%x]\n",
               (unsigned int)(p - pool_lo), mainzone->size);
    else
        printf("  OUTSIDE zone pool [%p, %p)\n", pool_lo, pool_hi);

    if (W_PointerInWadMapped(ptr, lump_name, &lump_off))
        printf("  ptr is inside WAD mapped region: lump='%.8s' +%u\n",
               lump_name, lump_off);

    hex_base = p - 32;
    printf("  32 bytes preceding ptr @%p:\n   ", (const void *)hex_base);
    for (i = 0; i < 32; i++) {
        printf("%02x ", hex_base[i]);
        if (i == 15) printf("\n   ");
    }
    printf("\n");
}

void Z_Free(void *ptr)
{
    memblock_t *block;
    memblock_t *other;

    Z_RequireAligned(ptr, "Z_Free: payload");
    block = Z_MemblockAt((byte *)ptr - sizeof(memblock_t), "Z_Free: block header");

    if (block->id != ZONEID) {
        Z_DumpFreeFailure(ptr, block, __builtin_return_address(0));
        I_Error("Z_Free: freed a pointer without ZONEID");
    }

    /* Z_Malloc stores (void **)2 as the "in-use, no owner" marker, so a
     * simple != NULL test would dereference a misaligned sentinel and
     * trap. Treat anything in the low 256 bytes as a non-pointer tag. */
    if (block->tag != PU_FREE && (uintptr_t)block->user > 0x100)
        *block->user = NULL;

    block->tag  = PU_FREE;
    block->user = NULL;
    block->id   = 0;

    other = block->prev;
    if (other->tag == PU_FREE) {
        other->size += block->size;
        other->next  = block->next;
        other->next->prev = other;
        if (block == mainzone->rover) mainzone->rover = other;
        block = other;
    }

    other = block->next;
    if (other->tag == PU_FREE) {
        block->size += other->size;
        block->next  = other->next;
        block->next->prev = block;
        if (other == mainzone->rover) mainzone->rover = block;
    }
}

void *Z_Malloc(int size, int tag, void *user)
{
    int         extra;
    memblock_t *start, *rover, *newblock, *base;
    void       *result;

    if (tag < 0 || tag >= PU_NUM_TAGS || tag == PU_FREE)
        I_Error("Z_Malloc: attempted to allocate a block with invalid tag: %i", tag);
    if (user == NULL && tag >= PU_PURGELEVEL)
        I_Error("Z_Malloc: an owner is required for purgable blocks");

    // Round to the zone alignment and account for the block header.
    size = (size + ZONE_ALIGN_MASK) & ~ZONE_ALIGN_MASK;
    size += sizeof(memblock_t);

    // Scan through the block list, looking for the first free block
    // large enough; purge all purgable blocks along the way.
    base = mainzone->rover;
    if (base->prev->tag == PU_FREE) base = base->prev;

    rover = base;
    start = base->prev;

    do {
        if (rover == start) {
            // Scanned all the way around the list without finding space.
            I_Error("Z_Malloc: failed on allocation of %i bytes", size);
        }

        if (rover->tag != PU_FREE) {
            if (rover->tag < PU_PURGELEVEL) {
                // hit a block that can't be purged — skip past it
                base  = rover->next;
                rover = rover->next;
            } else {
                // purge this block and coalesce
                base  = base->prev;
                Z_Free((byte *)rover + sizeof(memblock_t));
                base  = base->next;
                rover = base->next;
            }
        } else {
            rover = rover->next;
        }
    } while (base->tag != PU_FREE || base->size < size);

    // Found a big enough free block. Split if the remainder is useful.
    extra = base->size - size;
    if (extra > MINFRAGMENT) {
        newblock       = Z_MemblockAt((byte *)base + size, "Z_Malloc: split block");
        newblock->size = extra;
        newblock->tag  = PU_FREE;
        newblock->user = NULL;
        newblock->prev = base;
        newblock->next = base->next;
        newblock->next->prev = newblock;
        base->next = newblock;
        base->size = size;
    }

    result = (void *)((byte *)base + sizeof(memblock_t));

    if (user == NULL) {
        // Marker used to indicate a block that is in use and has no user.
        base->user = (void **)2;
    } else {
        base->user       = (void **)user;
        *(void **)user   = result;
    }
    base->tag = tag;
    base->id  = ZONEID;

    // Next allocation search starts just past this block.
    mainzone->rover = base->next;

    return result;
}

void Z_FreeTags(int lowtag, int hightag)
{
    memblock_t *block, *next;

    for (block = mainzone->blocklist.next;
         block != &mainzone->blocklist;
         block = next)
    {
        next = block->next;
        if (block->tag == PU_FREE) continue;
        if (block->tag >= lowtag && block->tag <= hightag)
            Z_Free((byte *)block + sizeof(memblock_t));
    }
}

void Z_DumpHeap(int lowtag, int hightag)
{
    memblock_t *block;

    printf("zone size: %i  location: %p\n", mainzone->size, mainzone);
    printf("tag range: %i to %i\n", lowtag, hightag);

    for (block = mainzone->blocklist.next; ; block = block->next) {
        if (block->tag >= lowtag && block->tag <= hightag)
            printf("block:%p    size:%7i    user:%p    tag:%3i\n",
                   block, block->size, block->user, block->tag);
        if (block->next == &mainzone->blocklist) break;
        if ((byte *)block + block->size != (byte *)block->next)
            printf("ERROR: block size does not touch the next block\n");
        if (block->next->prev != block)
            printf("ERROR: next block doesn't have proper back link\n");
        if (block->tag == PU_FREE && block->next->tag == PU_FREE)
            printf("ERROR: two consecutive free blocks\n");
    }
}

void Z_FileDumpHeap(FILE *f)
{
    memblock_t *block;

    fprintf(f, "zone size: %i  location: %p\n", mainzone->size, mainzone);

    for (block = mainzone->blocklist.next; ; block = block->next) {
        fprintf(f, "block:%p    size:%7i    user:%p    tag:%3i\n",
                block, block->size, block->user, block->tag);
        if (block->next == &mainzone->blocklist) break;
        if ((byte *)block + block->size != (byte *)block->next)
            fprintf(f, "ERROR: block size does not touch the next block\n");
        if (block->next->prev != block)
            fprintf(f, "ERROR: next block doesn't have proper back link\n");
        if (block->tag == PU_FREE && block->next->tag == PU_FREE)
            fprintf(f, "ERROR: two consecutive free blocks\n");
    }
}

void Z_CheckHeap(void)
{
    memblock_t *block;
    byte *pool_lo = (byte *)mainzone;
    byte *pool_hi = pool_lo + mainzone->size;

    if (!I_CheckZoneCanaries())
        I_Error("Z_CheckHeap: zone canary tripped — external writer stomped the pool boundary");

    for (block = mainzone->blocklist.next; ; block = block->next) {
        if (block->next == &mainzone->blocklist) break;
        if ((byte *)block < pool_lo || (byte *)block >= pool_hi)
            I_Error("Z_CheckHeap: block %p out of pool [%p,%p)", block, pool_lo, pool_hi);
        if (block->tag != PU_FREE && block->id != ZONEID)
            I_Error("Z_CheckHeap: in-use block %p has corrupt id=%08x tag=%d size=%d",
                    block, block->id, block->tag, block->size);
        if (block->size <= 0 || block->size > mainzone->size)
            I_Error("Z_CheckHeap: block %p has bogus size=%d", block, block->size);
        if ((byte *)block + block->size != (byte *)block->next)
            I_Error("Z_CheckHeap: block size does not touch the next block\n");
        if (block->next->prev != block)
            I_Error("Z_CheckHeap: next block doesn't have proper back link\n");
        if (block->tag == PU_FREE && block->next->tag == PU_FREE)
            I_Error("Z_CheckHeap: two consecutive free blocks\n");
    }
}

void Z_ChangeTag2(void *ptr, int tag, const char *file, int line)
{
    memblock_t *block;

    Z_RequireAligned(ptr, "Z_ChangeTag: payload");
    block = Z_MemblockAt((byte *)ptr - sizeof(memblock_t), "Z_ChangeTag: block header");

    if (block->id != ZONEID)
        I_Error("%s:%i: Z_ChangeTag: block without a ZONEID!", file, line);
    if (tag >= PU_PURGELEVEL && (uintptr_t)block->user < 0x100)
        I_Error("%s:%i: Z_ChangeTag: an owner is required for purgable blocks",
                file, line);
    block->tag = tag;
}

void Z_ChangeUser(void *ptr, void **user)
{
    memblock_t *block;

    Z_RequireAligned(ptr, "Z_ChangeUser: payload");
    block = Z_MemblockAt((byte *)ptr - sizeof(memblock_t), "Z_ChangeUser: block header");

    if (block->id != ZONEID)
        I_Error("Z_ChangeUser: Tried to change user for invalid block!");
    block->user = user;
    *user       = ptr;
}

int Z_FreeMemory(void)
{
    memblock_t *block;
    int         free_mem = 0;

    for (block = mainzone->blocklist.next;
         block != &mainzone->blocklist;
         block = block->next)
    {
        if (block->tag == PU_FREE || block->tag >= PU_PURGELEVEL)
            free_mem += block->size;
    }
    return free_mem;
}

unsigned int Z_ZoneSize(void)
{
    return mainzone->size;
}
