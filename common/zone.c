/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "cmd.h"
#include "common.h"
#include "console.h"
#include "mathlib.h"
#include "quakedef.h"
#include "sys.h"
#include "zone.h"

#define	DYNAMIC_SIZE	0x40000		/* 256k */
#define	ZONEID		0x1d4a11
#define MINFRAGMENT	64

typedef struct memblock_s {
    int size;		/* including the header and possibly tiny fragments */
    int tag;		/* a tag of 0 is a free block */
    int id;		/* should be ZONEID */
    struct memblock_s *next, *prev;
    int pad;		/* pad to 64 bit boundary */
} memblock_t;

typedef struct {
    int size;			/* total bytes malloced, including header */
    memblock_t blocklist;	/* start/end cap for linked list */
    memblock_t *rover;
} memzone_t;

static void Cache_FreeLow(int new_low_hunk);
static void Cache_FreeHigh(int new_high_hunk);
static void Cache_Dealloc(cache_user_t *c);

/*
 * ============================================================================
 *
 * ZONE MEMORY ALLOCATION
 *
 * There is never any space between memblocks, and there will never be two
 * contiguous free memblocks.
 *
 * The rover can be left pointing at a non-empty block
 *
 * The zone calls are pretty much only used for small strings and structures,
 * all big things are allocated on the hunk.
 * ============================================================================
 */

static memzone_t *mainzone;

static void Z_ClearZone(memzone_t *zone, int size);


/*
 * ========================
 * Z_ClearZone
 * ========================
 */
static void
Z_ClearZone(memzone_t *zone, int size)
{
    memblock_t *block;

    /*
     * set the entire zone to one free block
     */
    zone->blocklist.next = zone->blocklist.prev = block =
	(memblock_t *)((byte *)zone + sizeof(memzone_t));
    zone->blocklist.tag = 1;	/* in use block */
    zone->blocklist.id = 0;
    zone->blocklist.size = 0;
    zone->rover = block;
    zone->size = size;

    block->prev = block->next = &zone->blocklist;
    block->tag = 0;		/* free block */
    block->id = ZONEID;
    block->size = size - sizeof(memzone_t);
}


/*
 * ========================
 * Z_Free
 * ========================
 */
void
Z_Free(const void *ptr)
{
    memblock_t *block, *other;

    if (!ptr)
	Sys_Error("%s: NULL pointer", __func__);

    block = (memblock_t *)((const byte *)ptr - sizeof(memblock_t));
    if (block->id != ZONEID)
	Sys_Error("%s: freed a pointer without ZONEID", __func__);
    if (block->tag == 0)
	Sys_Error("%s: freed a freed pointer", __func__);

    block->tag = 0;		/* mark as free */

    other = block->prev;
    if (!other->tag) {		/* merge with previous free block */
	other->size += block->size;
	other->next = block->next;
	other->next->prev = other;
	if (block == mainzone->rover)
	    mainzone->rover = other;
	block = other;
    }

    other = block->next;
    if (!other->tag) {		/* merge the next free block onto the end */
	block->size += other->size;
	block->next = other->next;
	block->next->prev = block;
	if (other == mainzone->rover)
	    mainzone->rover = block;
    }
}


/*
 * ========================
 * Z_CheckHeap
 * ========================
 */
static void
Z_CheckHeap(void)
{
#ifdef DEBUG
    memblock_t *block;

    for (block = mainzone->blocklist.next;; block = block->next) {
	if (block->next == &mainzone->blocklist)
	    break;	/* all blocks have been hit */
	if ((byte *)block + block->size != (byte *)block->next)
	    Sys_Error("%s: block size does not touch the next block",
		      __func__);
	if (block->next->prev != block)
	    Sys_Error("%s: next block doesn't have proper back link",
		      __func__);
	if (!block->tag && !block->next->tag)
	    Sys_Error("%s: two consecutive free blocks", __func__);
    }
#endif
}


static void *
Z_TagMalloc(int size, int tag)
{
    int extra, *marker;
    memblock_t *start, *rover, *new, *base;

    if (!tag)
	Sys_Error("%s: tried to use a 0 tag", __func__);

    /*
     * Scan through the block list looking for the first free block of
     * sufficient size
     */
    size += sizeof(memblock_t);	/* account for size of block header */
    size += sizeof(int);	/* space for memory trash tester */
    size = (size + 7) & ~7;	/* align to 8-byte boundary */

    base = rover = mainzone->rover;
    start = base->prev;

    do {
	if (rover == start)	/* scaned all the way around the list */
	    return NULL;
	if (rover->tag)
	    base = rover = rover->next;
	else
	    rover = rover->next;
    } while (base->tag || base->size < size);

    /*
     * found a block big enough
     */
    extra = base->size - size;
    if (extra > MINFRAGMENT) {
	/* there will be a free fragment after the allocated block */
	new = (memblock_t *)((byte *)base + size);
	new->size = extra;
	new->tag = 0;		/* free block */
	new->prev = base;
	new->id = ZONEID;
	new->next = base->next;
	new->next->prev = new;
	base->next = new;
	base->size = size;
    }

    base->tag = tag;		   /* no longer a free block */
    mainzone->rover = base->next;  /* next allocation starts looking here */

    base->id = ZONEID;

    /* marker for memory trash testing */
    marker = (int *)((byte *)base + base->size - sizeof(int));
    *marker = ZONEID;

    return base + 1;
}


/*
 * ========================
 * Z_Malloc
 * ========================
 */
void *
Z_Malloc(int size)
{
    void *buf;

    Z_CheckHeap();		/* DEBUG */
    buf = Z_TagMalloc(size, 1);
    if (!buf)
	Sys_Error("%s: failed on allocation of %i bytes", __func__, size);
    memset(buf, 0, size);

    return buf;
}

/*
 * ========================
 * Z_Realloc
 * ========================
 */
void *
Z_Realloc(const void *ptr, int size)
{
    memblock_t *block;
    int orig_size;
    void *ret;

    if (!ptr)
	return Z_Malloc(size);

    block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
    if (block->id != ZONEID)
	Sys_Error("%s: realloced a pointer without ZONEID", __func__);
    if (!block->tag)
	Sys_Error("%s: realloced a freed pointer", __func__);

    orig_size = block->size;
    orig_size -= sizeof(memblock_t);
    orig_size -= sizeof(int); /* ZONEID marker */

    Z_Free(ptr);
    ret = Z_TagMalloc(size, 1);
    if (!ret)
	Sys_Error("%s: failed on allocation of %i bytes", __func__, size);
    if (ret != ptr)
	memmove(ret, ptr, qmin(orig_size, size));
    if (size > orig_size)
	memset((byte *)ret + orig_size, 0, size - orig_size);
    return ret;
}

/*
 * ========================
 * Z_Print
 * ========================
 */
static void
Z_Print(const memzone_t *zone, qboolean detailed)
{
    const memblock_t *block;
    unsigned free_blocks = 0, used_blocks = 0;
    size_t free_size = 0, used_size = 0;

    block = zone->blocklist.next;
    while (block) {
	if (detailed)
	    Con_Printf("block:%p    size:%7i    tag:%3i\n",
		       block, block->size, block->tag);

	/* Update totals */
	if (!block->tag) {
	    free_blocks++;
	    free_size += block->size;
	} else {
	    used_blocks++;
	    used_size += block->size;
	}

	if (block->next == &zone->blocklist)
	    break;		/* all blocks have been hit */
	if ((byte *)block + block->size != (byte *)block->next)
	    Con_Printf("ERROR: block size does not touch the next block\n");
	if (block->next->prev != block)
	    Con_Printf("ERROR: next block doesn't have proper back link\n");
	if (!block->tag && !block->next->tag)
	    Con_Printf("ERROR: two consecutive free blocks\n");

	block = block->next;
    }

    Con_Printf("zone size: %d  location: %p\n", zone->size, zone);
    Con_Printf("  %7lu bytes used in %d blocks\n",
	       (unsigned long)used_size, used_blocks);
    Con_Printf("  %7lu bytes available in %d blocks\n",
	       (unsigned long)free_size, free_blocks);
}

static void
Z_Zone_f(void)
{
    if (Cmd_Argc() == 2) {
	if (!strcmp(Cmd_Argv(1), "print")) {
	    Z_Print(mainzone, false);
	    return;
	}
	if (!strcmp(Cmd_Argv(1), "printall")) {
	    Z_Print(mainzone, true);
	    return;
	}
    }
    Con_Printf("Usage: zone print\n");
}

/* ======================================================================= */

#define HUNK_SENTINAL 0x1df001ed

typedef struct {
    int sentinal;
    int size; /* including sizeof(hunk_t) */
    char name[HUNK_NAMELEN]; /* not necessarily NULL terminated */
} hunk_t;

static struct {
    byte *base;
    void *lowbase;
    int size;
    int lowbytes;
    int highbytes;
    int tempmark;
} hunkstate;

/*
 * ==============
 * Hunk_Check
 *
 * Run consistancy and sentinal trashing checks
 * ==============
 */
void
Hunk_Check(void)
{
    hunk_t *hunk;

    hunk = (hunk_t *)hunkstate.base;
    while ((byte *)hunk != hunkstate.base + hunkstate.lowbytes) {
	if (hunk->sentinal != HUNK_SENTINAL)
	    Sys_Error("%s: trashed sentinal", __func__);
	if (hunk->size < sizeof(hunk_t) ||
	    hunk->size + (byte *)hunk - hunkstate.base > hunkstate.size)
	    Sys_Error("%s: bad size", __func__);
	hunk = (hunk_t *)((byte *)hunk + hunk->size);
    }
}


/*
 * ==============
 * Hunk_Print
 *
 * If "all" is specified, every single allocation is printed.
 * Otherwise, allocations with the same name will be totaled up before
 * printing.
 * ==============
 */
static void
Hunk_Print(qboolean all)
{
    const hunk_t *hunk, *next, *endlow, *starthigh, *endhigh;
    int count, sum, pwidth;
    int totalblocks;
    char safename[HUNK_NAMELEN + 1];

    count = 0;
    sum = 0;
    totalblocks = 0;
    /*
     * We use a copy of the hunk name with an extra zero terminator
     * for printing, in case of hunk corruption.
     */
    memset(safename, 0, sizeof(safename));

    /* Don't put in wide spaces if not printing pointers */
    pwidth = all ? (sizeof(void *) * 2 + 2) : 8;

    endlow = (hunk_t *)(hunkstate.base + hunkstate.lowbytes);
    endhigh = (hunk_t *)(hunkstate.base + hunkstate.size);
    starthigh = (hunk_t *)((byte *)endhigh - hunkstate.highbytes);

    Con_Printf("%*s :%10i total hunk size\n", pwidth, "", hunkstate.size);
    Con_Printf("-------------------------\n");

    next = (hunk_t *)hunkstate.base;
    while (1) {
	hunk = next;

	/*
	 * skip to the high hunk if done with low hunk
	 */
	if (hunk == endlow) {
	    int mem = hunkstate.size - hunkstate.lowbytes - hunkstate.highbytes;
	    Con_Printf("-------------------------\n"
		       "%*s :%10i REMAINING\n"
		       "-------------------------\n",
		       pwidth, "", mem);
	    hunk = starthigh;
	}

	/*
	 * if totally done, break
	 */
	if (hunk == endhigh)
	    break;

	/*
	 * run consistancy checks
	 */
	if (hunk->sentinal != HUNK_SENTINAL)
	    Sys_Error("%s: trashed sentinal", __func__);
	if (hunk->size < 16)
	    Sys_Error("%s: bad size", __func__);
	if (hunk->size + (byte *)hunk - hunkstate.base > hunkstate.size)
	    Sys_Error("%s: bad size", __func__);

	/*
	 * print the single block
	 */
	memcpy(safename, hunk->name, HUNK_NAMELEN);
	if (all)
	    Con_Printf("%*p :%10i %-*s\n", pwidth, hunk, hunk->size,
		       HUNK_NAMELEN, safename);

	next = (hunk_t *)((byte *)hunk + hunk->size);
	count++;
	totalblocks++;
	sum += hunk->size;

	if (next != endlow && next != endhigh)
	    if (!strncmp(hunk->name, next->name, HUNK_NAMELEN))
		continue;
	/*
	 * print the total
	 */
	if (!all)
	    Con_Printf("%*s :%10i %-*s (TOTAL)\n",
		       pwidth, "", sum, HUNK_NAMELEN, safename);
	count = 0;
	sum = 0;
    }
    Con_Printf("-------------------------\n");
    Con_Printf("%8i total blocks\n", totalblocks);
}

static void
Hunk_f(void)
{
    if (Cmd_Argc() == 2) {
	if (!strcmp(Cmd_Argv(1), "print")) {
	    Hunk_Print(false);
	    return;
	}
	if (!strcmp(Cmd_Argv(1), "printall")) {
	    Hunk_Print(true);
	    return;
	}
    }
    Con_Printf("Usage: hunk print|printall\n");
}

/*
 * ===================
 * Hunk_AllocName
 * ===================
 */
void *
Hunk_AllocName(int size, const char *name)
{
    hunk_t *hunk;
    size_t freebytes;

#ifdef PARANOID
    Hunk_Check();
#endif

    if (size < 0)
	Sys_Error("%s: bad size: %i", __func__, size);

    size = sizeof(hunk_t) + ((size + 15) & ~15);

    freebytes = hunkstate.size - hunkstate.lowbytes - hunkstate.highbytes;
    if (freebytes < size) {
	int extra, newmem;

	/* check how much was needed, but recommend at least 1/4 extra */
	extra = (size - freebytes + (1 << 20) - 1) >> 20;
	extra = qmax(extra, hunkstate.size >> 22);

	/* Round memory recommendation to a multiple of 16MB */
	newmem = (((hunkstate.size + (1 << 20) - 1) >> 20) + extra + 15) & ~15;

	Sys_Error ("%s: failed on %d bytes.\n\n"
		   "Not enough RAM allocated (%4.1f MB).\n"
		   "Try starting using \"-mem %d\" on the command line.",
		   __func__, size, (float)hunkstate.size / 1024 / 1024, newmem);
    }

    hunk = (hunk_t *)(hunkstate.base + hunkstate.lowbytes);
    hunkstate.lowbytes += size;

    Cache_FreeLow(hunkstate.lowbytes);

    memset(hunk, 0, size);

    hunk->size = size;
    hunk->sentinal = HUNK_SENTINAL;
    memset(hunk->name, 0, HUNK_NAMELEN);
    memcpy(hunk->name, name, qmin((int)strlen(name), HUNK_NAMELEN));

    /* Save a copy of the allocated address */
    hunkstate.lowbase = hunk + 1;

    return hunkstate.lowbase;
}

/*
 * Hunk_AllocExtend
 *
 * Extend the current hunk allocation, rather than allocating a new
 * hunk_t header. Useful for sequencing lots of small allocations.
 */
void *
Hunk_AllocExtend(const void *base, int size)
{
    hunk_t *hunk;
    void *memptr;

    if (base != hunkstate.lowbase)
	Sys_Error("%s: base != current base", __func__);

    if (size < 0)
	Sys_Error("%s: bad size: %i", __func__, size);

    size = (size + 15) & ~15;

    if (hunkstate.size - hunkstate.lowbytes - hunkstate.highbytes < size) {
	/* Sys_Error ("%s: failed on %i bytes", __func__, size); */
#ifdef _WIN32
	Sys_Error("Not enough RAM allocated.  Try starting using "
		  "\"-heapsize 16000\" on the command line.");
#else
	Sys_Error("Not enough RAM allocated.  Try starting using "
		  "\"-mem 16\" on the command line.");
#endif
    }

    hunk = (hunk_t *)base - 1;
    if (hunk->sentinal != HUNK_SENTINAL)
	Sys_Error("%s: bad sentinal (%d)", __func__, hunk->sentinal);

    hunkstate.lowbytes += size;
    Cache_FreeLow(hunkstate.lowbytes);

    memptr = (byte *)hunk + hunk->size;
    memset(memptr, 0, size);
    hunk->size += size;

    return memptr;
}

/*
 * ===================
 * Hunk_Alloc
 * ===================
 */
void *
Hunk_Alloc(int size)
{
    return Hunk_AllocName(size, "unknown");
}

int
Hunk_LowMark(void)
{
    return hunkstate.lowbytes;
}

void
Hunk_FreeToLowMark(int mark)
{
    if (mark < 0 || mark > hunkstate.lowbytes)
	Sys_Error("%s: bad mark %i", __func__, mark);
    memset(hunkstate.base + mark, 0, hunkstate.lowbytes - mark);
    hunkstate.lowbytes = mark;
}

int
Hunk_HighMark(void)
{
    if (hunkstate.tempmark) {
	const int tempmark = hunkstate.tempmark;
	hunkstate.tempmark = 0;
	Hunk_FreeToHighMark(tempmark);
    }

    return hunkstate.highbytes;
}

void
Hunk_FreeToHighMark(int mark)
{
    byte *base;

    if (hunkstate.tempmark) {
	const int tempmark = hunkstate.tempmark;
	hunkstate.tempmark = 0;
	Hunk_FreeToHighMark(tempmark);
    }
    if (mark < 0 || mark > hunkstate.highbytes)
	Sys_Error("%s: bad mark %i", __func__, mark);

    base = hunkstate.base + hunkstate.size - hunkstate.highbytes;
    memset(base, 0, hunkstate.highbytes - mark);
    hunkstate.highbytes = mark;
}


/*
 * ===================
 * Hunk_HighAllocName
 * ===================
 */
void *
Hunk_HighAllocName(int size, const char *name)
{
    hunk_t *hunk;

    if (size < 0)
	Sys_Error("%s: bad size: %i", __func__, size);

    if (hunkstate.tempmark) {
	const int tempmark = hunkstate.tempmark;
	hunkstate.tempmark = 0;
	Hunk_FreeToHighMark(tempmark);
    }
#ifdef PARANOID
    Hunk_Check();
#endif

    size = sizeof(hunk_t) + ((size + 15) & ~15);

    if (hunkstate.size - hunkstate.lowbytes - hunkstate.highbytes < size) {
	Con_Printf("Hunk_HighAlloc: failed on %i bytes\n", size);
	return NULL;
    }

    hunkstate.highbytes += size;
    Cache_FreeHigh(hunkstate.highbytes);

    hunk = (hunk_t *)(hunkstate.base + hunkstate.size - hunkstate.highbytes);

    memset(hunk, 0, size);
    hunk->size = size;
    hunk->sentinal = HUNK_SENTINAL;
    snprintf(hunk->name, sizeof(hunk->name), "%s", name);

    return (void *)(hunk + 1);
}


/*
 * =================
 * Hunk_TempAlloc
 *
 * Return space from the top of the hunk
 * =================
 */
void *
Hunk_TempAlloc(int size)
{
    void *buf;
    int mark;

    size = (size + 15) & ~15;

    if (hunkstate.tempmark) {
	const int tempmark = hunkstate.tempmark;
	hunkstate.tempmark = 0;
	Hunk_FreeToHighMark(tempmark);
    }

    mark = Hunk_HighMark();
    buf = Hunk_HighAllocName(size, "temp");
    hunkstate.tempmark = mark;

    return buf;
}

/*
 * =====================
 * Hunk_TempAllocExtend
 *
 * Extend the existing temp hunk allocation.
 * Size is the number of extra bytes required
 * =====================
 */
void *
Hunk_TempAllocExtend(int size)
{
    hunk_t *old, *new;

    if (!hunkstate.tempmark)
	Sys_Error("%s: temp hunk not active", __func__);

    old = (hunk_t *)(hunkstate.base + hunkstate.size - hunkstate.highbytes);
    if (old->sentinal != HUNK_SENTINAL)
	Sys_Error("%s: old sentinal trashed\n", __func__);
    if (strncmp(old->name, "temp", HUNK_NAMELEN))
	Sys_Error("%s: old hunk name trashed\n", __func__);

    size = (size + 15) & ~15;
    if (hunkstate.size - hunkstate.lowbytes - hunkstate.highbytes < size) {
	Con_Printf("%s: failed on %i bytes\n", __func__, size);
	return NULL;
    }

    hunkstate.highbytes += size;
    Cache_FreeHigh(hunkstate.highbytes);

    new = (hunk_t *)(hunkstate.base + hunkstate.size - hunkstate.highbytes);
    memmove(new, old, sizeof(hunk_t));
    new->size += size;

    return new + 1;
}

/*
 * ===========================================================================
 *
 * CACHE MEMORY
 *
 * ===========================================================================
 */

#define CACHE_NAMELEN 32

typedef struct cache_system_s {
    int size;			/* including this header */
    cache_user_t *user;
    char name[CACHE_NAMELEN];
    struct cache_system_s *prev, *next;
    struct cache_system_s *lru_prev, *lru_next;	/* for LRU flushing */
} cache_system_t;

static cache_system_t cache_head;
static cache_system_t *Cache_TryAlloc(int size, qboolean nobottom);

static inline cache_system_t *
Cache_System(const cache_user_t *c)
{
    return (cache_system_t *)((byte *)c->data - c->pad) - 1;
}

static inline void *
Cache_Data(const cache_system_t *c)
{
    return (byte *)(c + 1) + c->user->pad;
}

#ifdef DEBUG
void
Cache_CheckLinks(void)
{
    const cache_system_t *cache;

    cache = cache_head.next;
    while (cache != &cache_head)
	cache = cache->next;

    cache = cache_head.prev;
    while (cache != &cache_head)
	cache = cache->prev;

    cache = cache_head.lru_next;
    while (cache != &cache_head)
	cache = cache->lru_next;

    cache = cache_head.lru_prev;
    while (cache != &cache_head)
	cache = cache->lru_prev;
}
#endif

/*
 * ===========
 * Cache_Move
 * ===========
 */
static void
Cache_Move(cache_system_t *old_cs)
{
    cache_system_t *new_cs;

    /* we are clearing up space at the bottom, so only allocate it late */
    new_cs = Cache_TryAlloc(old_cs->size, true);
    if (new_cs) {
	memcpy(new_cs + 1, old_cs + 1, old_cs->size - sizeof(cache_system_t));
	new_cs->user = old_cs->user;
	memcpy(new_cs->name, old_cs->name, sizeof(new_cs->name));
	Cache_Dealloc(old_cs->user);
	new_cs->user->data = Cache_Data(new_cs);
    } else {
	/* tough luck... */
	Cache_Free(old_cs->user);
    }
}

/*
 * ============
 * Cache_FreeLow
 *
 * Throw things out until the hunk can be expanded to the given point
 * ============
 */
static void
Cache_FreeLow(int new_low_hunk)
{
    cache_system_t *c;

    while (1) {
	c = cache_head.next;
	if (c == &cache_head)
	    return;		/* nothing in cache at all */
	if ((byte *)c >= hunkstate.base + new_low_hunk)
	    return;		/* there is space to grow the hunk */
	Cache_Move(c);		/* reclaim the space */
    }
}

/*
 * ============
 * Cache_FreeHigh
 *
 * Throw things out until the hunk can be expanded to the given point
 * ============
 */
static void
Cache_FreeHigh(int new_high_hunk)
{
    cache_system_t *c, *prev;

    prev = NULL;
    while (1) {
	c = cache_head.prev;
	if (c == &cache_head)
	    return;		/* nothing in cache at all */
	if ((byte *)c + c->size <= hunkstate.base + hunkstate.size - new_high_hunk)
	    return;		/* there is space to grow the hunk */
	if (c == prev)
	    Cache_Free(c->user);	/* didn't move out of the way */
	else {
	    Cache_Move(c);	/* try to move it */
	    prev = c;
	}
    }
}

static void
Cache_UnlinkLRU(cache_system_t *cs)
{
    if (!cs->lru_next || !cs->lru_prev)
	Sys_Error("%s: NULL link", __func__);

    cs->lru_next->lru_prev = cs->lru_prev;
    cs->lru_prev->lru_next = cs->lru_next;

    cs->lru_prev = cs->lru_next = NULL;
}

static void
Cache_MakeLRU(cache_system_t *cs)
{
    if (cs->lru_next || cs->lru_prev)
	Sys_Error("%s: active link", __func__);

    cache_head.lru_next->lru_prev = cs;
    cs->lru_next = cache_head.lru_next;
    cs->lru_prev = &cache_head;
    cache_head.lru_next = cs;
}

/*
 * ============
 * Cache_TryAlloc
 *
 * Looks for a free block of memory between the high and low hunk marks
 * Size should already include the header and padding
 * ============
 */
static cache_system_t *
Cache_TryAlloc(int size, qboolean nobottom)
{
    cache_system_t *cs, *new;

    /* is the cache completely empty? */
    if (!nobottom && cache_head.prev == &cache_head) {
	if (hunkstate.size - hunkstate.highbytes - hunkstate.lowbytes < size)
	    Sys_Error("%s: %i is greater than free hunk", __func__, size);

	new = (cache_system_t *)(hunkstate.base + hunkstate.lowbytes);
	memset(new, 0, sizeof(*new));
	new->size = size;

	cache_head.prev = cache_head.next = new;
	new->prev = new->next = &cache_head;

	Cache_MakeLRU(new);
	return new;
    }

    /* search from the bottom up for space */
    new = (cache_system_t *)(hunkstate.base + hunkstate.lowbytes);
    cs = cache_head.next;

    do {
	if (!nobottom || cs != cache_head.next) {
	    if ((byte *)cs - (byte *)new >= size) {	/* found space */
		memset(new, 0, sizeof(*new));
		new->size = size;

		new->next = cs;
		new->prev = cs->prev;
		cs->prev->next = new;
		cs->prev = new;

		Cache_MakeLRU(new);

		return new;
	    }
	}

	/* continue looking */
	new = (cache_system_t *)((byte *)cs + cs->size);
	cs = cs->next;

    } while (cs != &cache_head);

    /* try to allocate one at the very end */
    if (hunkstate.base + hunkstate.size - hunkstate.highbytes - (byte *)new >= size) {
	memset(new, 0, sizeof(*new));
	new->size = size;

	new->next = &cache_head;
	new->prev = cache_head.prev;
	cache_head.prev->next = new;
	cache_head.prev = new;

	Cache_MakeLRU(new);

	return new;
    }

    return NULL;		/* couldn't allocate */
}

/*
 * ============
 * Cache_Flush
 *
 * Throw everything out, so new data will be demand cached
 * ============
 */
void
Cache_Flush(void)
{
    while (cache_head.next != &cache_head)
	Cache_Free(cache_head.next->user);	/* reclaim the space */
}

/*
 * ============
 * Cache_Print
 * ============
 */
static void
Cache_Print(void)
{
    cache_system_t *cd;

    for (cd = cache_head.next; cd != &cache_head; cd = cd->next) {
	Con_Printf("%8i : %s\n", cd->size, cd->name);
    }
}

/*
 * ============
 * Cache_Report
 * ============
 */
void
Cache_Report(void)
{
    Con_DPrintf("%4.1f megabyte data cache\n",
		(hunkstate.size - hunkstate.highbytes -
		 hunkstate.lowbytes) / (float)(1024 * 1024));
}


/* FIXME - Unused? */
#if 0
/*
 * ============
 * Cache_Compact
 * ============
 */
static void
Cache_Compact(void)
{
}
#endif

/*
 * ============
 * Cache_Init
 * ============
 */
static void
Cache_Init(void)
{
    cache_head.next = cache_head.prev = &cache_head;
    cache_head.lru_next = cache_head.lru_prev = &cache_head;
}

/*
 * ==============
 * Cache_Dealloc
 *
 * Frees the memory and removes it from the LRU list
 * ==============
 */
static void
Cache_Dealloc(cache_user_t *c)
{
    cache_system_t *cs;

    if (!c->data)
	Sys_Error("%s: not allocated", __func__);

    cs = Cache_System(c);
    cs->prev->next = cs->next;
    cs->next->prev = cs->prev;
    cs->next = cs->prev = NULL;

    Cache_UnlinkLRU(cs);
}

/*
 * ==============
 * Cache_Free
 *
 * Call the destructor before freeing the cache entry
 * ==============
 */
void
Cache_Free(cache_user_t *c)
{
    /* Cleanup the user data */
    if (c->destructor) {
	c->destructor(c);
	c->destructor = NULL;
    }

    Cache_Dealloc(c);

    c->pad = 0;
    c->data = NULL;
}

/*
 * ==============
 * Cache_Check
 * ==============
 */
void *
Cache_Check(const cache_user_t *c)
{
    cache_system_t *cs;

    if (!c->data)
	return NULL;

    cs = Cache_System(c);

    /* move to head of LRU */
    Cache_UnlinkLRU(cs);
    Cache_MakeLRU(cs);

    return c->data;
}


/*
 * ==============
 * Cache_Alloc
 * ==============
 */
void *
Cache_Alloc(cache_user_t *c, int size, const char *name)
{
    return Cache_AllocPadded(c, 0, size, name);
}

/*
 * ==============
 * Cache_AllocPadded
 * ==============
 */
void *
Cache_AllocPadded(cache_user_t *c, int pad, int size, const char *name)
{
    cache_system_t *cs;

    if (c->data)
	Sys_Error("%s: already allocated", __func__);

    if (size <= 0)
	Sys_Error("%s: size %i", __func__, size);

    size = (size + pad + sizeof(cache_system_t) + 15) & ~15;

    /* find memory for it */
    while (1) {
	cs = Cache_TryAlloc(size, false);
	if (cs) {
	    strncpy(cs->name, name, sizeof(cs->name) - 1);
	    cs->user = c;
	    c->pad = pad;
	    c->data = Cache_Data(cs);
	    c->destructor = NULL;
	    break;
	}
	/* free the least recently used cache data */
	if (cache_head.lru_prev == &cache_head)
	    Sys_Error("%s: out of memory", __func__);
	/* not enough memory at all */
	Cache_Free(cache_head.lru_prev->user);
    }

    return Cache_Check(c);
}

static void
Cache_f(void)
{
    if (Cmd_Argc() == 2) {
	if (!strcmp(Cmd_Argv(1), "print")) {
	    Cache_Print();
	    return;
	}
	if (!strcmp(Cmd_Argv(1), "flush")) {
	    Cache_Flush();
	    return;
	}
    }
    Con_Printf("Usage: cache print|flush\n");
}

/* ========================================================================= */

size_t
Memory_GetSize(void)
{
    int parm;

    /* The -mem parameter gives heap size in megabytes */
    parm = COM_CheckParm("-mem");
    if (parm && com_argc > parm + 1)
	return (size_t)atoi(com_argv[parm + 1]) << 20;

    /* The -heapsize parameter gives the heap size in kilobytes */
    parm = COM_CheckParm("-heapsize");
    if (parm && com_argc > parm + 1)
	return (size_t)atoi(com_argv[parm + 1]) << 10;

    /* Default memory is now 128MB */
    return (size_t)128 << 20;
}

/*
 * ========================
 * Memory_Init
 * ========================
 */
void
Memory_Init(void *buf, int size)
{
    int p;
    int zonesize = DYNAMIC_SIZE;

    hunkstate.base = buf;
    hunkstate.size = size;
    hunkstate.lowbytes = 0;
    hunkstate.highbytes = 0;
    hunkstate.tempmark = 0;

    Cache_Init();
    p = COM_CheckParm("-zone");
    if (p) {
	if (p < com_argc - 1)
	    zonesize = Q_atoi(com_argv[p + 1]) * 1024;
	else
	    Sys_Error("%s: you must specify a size in KB after -zone",
		      __func__);
    }
    mainzone = Hunk_AllocName(zonesize, "zone");
    Z_ClearZone(mainzone, zonesize);

    /* Needs to be added after the zone init... */
    Cmd_AddCommand("flush", Cache_Flush);
    Cmd_AddCommand("hunk", Hunk_f);
    Cmd_AddCommand("zone", Z_Zone_f);
    Cmd_AddCommand("cache", Cache_f);
}
