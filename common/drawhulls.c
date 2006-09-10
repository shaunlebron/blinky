/*
Copyright (C) 2005 Kevin Shanahan
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

/* drawhulls.c - make the collision hulls drawable */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "console.h"
#include "glquake.h" /* FIXME - make usable in software mode too */
#include "mathlib.h"
#include "sys.h"


struct list_node {
    struct list_node *next;
    struct list_node *prev;
};

#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/* Iterate over each entry in the list */
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_entry(pos->member.next, typeof(*pos), member))

/* Iterate over the list, safe for removal of entries */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
	     n = list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

#define LIST_HEAD_INIT(name) { &(name), &(name) }

static inline void
list_add__(struct list_node *new,
	   struct list_node *prev,
	   struct list_node* next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/* Add the new entry after the give list entry */
static inline void
list_add(struct list_node *new, struct list_node *head)
{
    list_add__(new, head, head->next);
}

/* Add the new entry before the given list entry (list is circular) */
static inline void
list_add_tail(struct list_node *new, struct list_node *head)
{
    list_add__(new, head->prev, head);
}

static inline void
list_del(struct list_node *entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
}

typedef struct winding_s {
    const mplane_t *plane;
    struct winding_s *pair;
    struct list_node chain;
    int numpoints;
    vec3_t points[0];		/* variable sized */
} winding_t;

static winding_t *
winding_alloc(unsigned numverts)
{
    return malloc(sizeof(winding_t) + numverts * sizeof(vec3_t));
}

static winding_t *
winding_copy(winding_t *w)
{
    winding_t *neww;

    neww = winding_alloc(w->numpoints);
    memcpy(neww, w, sizeof(winding_t) + w->numpoints * sizeof(vec3_t));

    return neww;
}

static void
winding_reverse(winding_t *w)
{
    vec3_t v;
    int i;

    for (i = 0; i < w->numpoints / 2; i++) {
	VectorCopy(w->points[i], v);
	VectorCopy(w->points[w->numpoints - i - 1], w->points[i]);
	VectorCopy(v, w->points[w->numpoints -i -1]);
    }
}

/*
 * winding_shrink
 *
 * Takes an over-allocated winding and allocates a new winding with just the
 * required number of points. The input winding is freed.
 */
static winding_t *
winding_shrink(winding_t *w)
{
    winding_t *neww;
    int copysize;

    neww = winding_alloc(w->numpoints);
    copysize = sizeof(winding_t) + w->numpoints * sizeof(vec3_t);

    if (copysize > 0)
	memcpy(neww, w, copysize);
    free(w);

    return neww;
}

#define BOGUS_RANGE ((vec_t)18000.0)

/*
====================
winding_for_plane
====================
*/
static winding_t *
winding_for_plane(const mplane_t *p)
{
    int i, axis;
    vec_t max, v;
    vec3_t org, vright, vup;
    winding_t *w;

    // find the major axis
    max = -BOGUS_RANGE;
    axis = -1;
    for (i = 0; i < 3; i++) {
	v = fabs(p->normal[i]);
	if (v > max) {
	    axis = i;
	    max = v;
	}
    }
    VectorCopy(vec3_origin, vup);
    switch (axis) {
    case 0:
    case 1:
	vup[2] = 1;
	break;
    case 2:
	vup[0] = 1;
	break;
    default:
	return NULL;
    }
    v = DotProduct(vup, p->normal);
    VectorMA(vup, -v, p->normal, vup);
    VectorNormalize(vup);
    VectorScale(p->normal, p->dist, org);
    CrossProduct(vup, p->normal, vright);
    VectorScale(vup, 8192, vup);
    VectorScale(vright, 8192, vright);

    // project a really big axis aligned box onto the plane
    w = winding_alloc(4);
    memset(w->points, 0, 4 * sizeof(vec3_t));

    w->numpoints = 4;
    w->plane = p;

    VectorSubtract(org, vright, w->points[0]);
    VectorAdd(w->points[0], vup, w->points[0]);
    VectorAdd(org, vright, w->points[1]);
    VectorAdd(w->points[1], vup, w->points[1]);
    VectorAdd(org, vright, w->points[2]);
    VectorSubtract(w->points[2], vup, w->points[2]);
    VectorSubtract(org, vright, w->points[3]);
    VectorSubtract(w->points[3], vup, w->points[3]);

    return w;
}

/*
 * ===========================
 * Helper for for the clipping functions
 *  (winding_clip, winding_split)
 * ===========================
 */
static void
CalcSides(const winding_t *in, const mplane_t *split,
	  int *sides, vec_t *dists, int counts[3], vec_t epsilon)
{
    int i;
    const vec_t *p;

    counts[0] = counts[1] = counts[2] = 0;

    switch (split->type) {
    case PLANE_X:
    case PLANE_Y:
    case PLANE_Z:
	p = in->points[0] + split->type;
	for (i = 0; i < in->numpoints; ++i, p += 3) {
	    const vec_t dot = *p - split->dist;

	    dists[i] = dot;
	    if (dot > epsilon)
		sides[i] = SIDE_FRONT;
	    else if (dot < -epsilon)
		sides[i] = SIDE_BACK;
	    else
		sides[i] = SIDE_ON;
	    counts[sides[i]]++;
	}
	break;
    default:
	p = in->points[0];
	for (i = 0; i < in->numpoints; ++i, p += 3) {
	    const vec_t dot = DotProduct(split->normal, p) - split->dist;

	    dists[i] = dot;
	    if (dot > epsilon)
		sides[i] = SIDE_FRONT;
	    else if (dot < -epsilon)
		sides[i] = SIDE_BACK;
	    else
		sides[i] = SIDE_ON;
	    counts[sides[i]]++;
	}
	break;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];
}

static void
PushToPlaneAxis(vec_t *v, const mplane_t *p)
{
    const int t = p->type % 3;

    v[t] = (p->dist - p->normal[(t + 1) % 3] * v[(t + 1) % 3] -
	    p->normal[(t + 2) % 3] * v[(t + 2) % 3]) / p->normal[t];
}

/*
==================
winding_clip

Clips the winding to the plane, returning the new winding on 'side'.
Frees the input winding.
If keepon is true, an exactly on-plane winding will be saved, otherwise
  it will be clipped away.
==================
*/
static winding_t *
winding_clip(winding_t *in, const mplane_t *split,
	     qboolean keepon, int side, vec_t epsilon /* = ON_EPSILON */ )
{
    vec_t *dists;
    int *sides;
    int counts[3];
    vec_t dot;
    int i, j;
    winding_t *neww;
    vec_t *p1, *p2, *mid;
    int maxpts;
    int insize = in->numpoints; /* save for dists/sides free */

    dists = malloc((insize + 1) * sizeof(vec_t));
    sides = malloc((insize + 1) * sizeof(int));

    CalcSides(in, split, sides, dists, counts, epsilon);

    if (keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK]) {
	neww = in;
	goto out_free;
    }
    if (!counts[side]) {
	free(in);
	neww = NULL;
	goto out_free;
    }
    if (!counts[side ^ 1]) {
	neww = in;
	goto out_free;
    }

    maxpts = in->numpoints + 4;
    neww = winding_alloc(maxpts);
    neww->numpoints = 0;
    neww->plane = in->plane;

    for (i = 0; i < in->numpoints; i++) {
	p1 = in->points[i];

	if (sides[i] == SIDE_ON) {
	    _VectorCopy(p1, neww->points[neww->numpoints++]);
	    continue;
	}
	if (sides[i] == side)
	    _VectorCopy(p1, neww->points[neww->numpoints++]);

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	// generate a split point
	p2 = in->points[(i + 1) % in->numpoints];
	mid = neww->points[neww->numpoints++];

	dot = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {
	    // avoid round off error when possible
	    if (in->plane->normal[j] == 1.0)
		mid[j] = in->plane->dist;
	    else if (in->plane->normal[j] == -1.0)
		mid[j] = -in->plane->dist;
	    else if (split->normal[j] == 1.0)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1.0)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + dot * (p2[j] - p1[j]);
	}
	if (in->plane->type < 3)
	    PushToPlaneAxis(mid, in->plane);
    }

    // free the original winding
    free(in);

    // Shrink the winding back to just what it needs...
    neww = winding_shrink(neww);

  out_free:
    free(dists);
    free(sides);

    return neww;
}

/*
==================
winding_split

Splits a winding by a plane, producing one or two windings.  The
original winding is not damaged or freed.  If only on one side, the
returned winding will be the input winding.  If on both sides, two
new windings will be created.
==================
*/
static void
winding_split(winding_t *in, const mplane_t *split,
	      winding_t **pfront, winding_t **pback)
{
    vec_t *dists;
    int *sides;
    int counts[3];
    vec_t dot;
    int i, j;
    winding_t *front, *back;
    vec_t *p1, *p2, *mid;
    int maxpts;

    dists = malloc((in->numpoints + 1) * sizeof(vec_t));
    sides = malloc((in->numpoints + 1) * sizeof(int));

    CalcSides(in, split, sides, dists, counts, 0.0001 /* ON_EPSILON */);

    assert(counts[0] || counts[1]);

    if (!counts[0] && !counts[1]) {
	/* Winding on the split plane - return copies on both sides */
	*pfront = winding_copy(in);
	*pback = winding_copy(in);
	goto out_free;
    }
    if (!counts[0]) {
	*pfront = NULL;
	*pback = in;
	goto out_free;
    }
    if (!counts[1]) {
	*pfront = in;
	*pback = NULL;
	goto out_free;
    }

    maxpts = in->numpoints + 4;
    front = winding_alloc(maxpts);
    front->numpoints = 0;
    front->plane = in->plane;
    back = winding_alloc(maxpts);
    back->numpoints = 0;
    back->plane = in->plane;

    for (i = 0; i < in->numpoints; i++) {
	p1 = in->points[i];

	if (sides[i] == SIDE_ON) {
	    _VectorCopy(p1, front->points[front->numpoints++]);
	    _VectorCopy(p1, back->points[back->numpoints++]);
	    continue;
	}

	if (sides[i] == SIDE_FRONT)
	    _VectorCopy(p1, front->points[front->numpoints++]);
	else if (sides[i] == SIDE_BACK)
	    _VectorCopy(p1, back->points[back->numpoints++]);

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	// generate a split point
	p2 = in->points[(i + 1) % in->numpoints];
	mid = front->points[front->numpoints++];

	dot = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {
	    // avoid round off error when possible
	    if (in->plane->normal[j] == 1.0)
		mid[j] = in->plane->dist;
	    if (in->plane->normal[j] == -1.0)
		mid[j] = -in->plane->dist;
	    else if (split->normal[j] == 1.0)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1.0)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + dot * (p2[j] - p1[j]);
	}
	if (in->plane->type < 3)
	    PushToPlaneAxis(mid, in->plane);
	_VectorCopy(mid, back->points[back->numpoints++]);
    }

    *pfront = winding_shrink(front);
    *pback = winding_shrink(back);

  out_free:
    free(dists);
    free(sides);
}


/* ------------------------------------------------------------------------- */

/*
 * This is a stack of the clipnodes we have traversed
 * "sides" indicates which side we went down each time
 */
#define MAX_CLIPNODE_DEPTH 256
static dclipnode_t *node_stack[MAX_CLIPNODE_DEPTH];
static int side_stack[MAX_CLIPNODE_DEPTH];
static unsigned node_stack_depth;
static unsigned num_hull_polys;
static struct list_node hull_polys = LIST_HEAD_INIT(hull_polys);

static void
push_node(dclipnode_t *node, int side)
{
    if (node_stack_depth == MAX_CLIPNODE_DEPTH)
	Sys_Error("%s: node_depth == MAX_CLIPNODE_DEPTH\n", __func__);

    node_stack[node_stack_depth] = node;
    side_stack[node_stack_depth] = side;
    node_stack_depth++;
}

static void
pop_node(void)
{
    if (!node_stack_depth)
	Sys_Error("%s: attempted pop when node stack is empty\n", __func__);

    node_stack_depth--;
}

static void
free_hull_polys(void)
{
    winding_t *w, *next;

    list_for_each_entry_safe(w, next, &hull_polys, chain) {
	list_del(&w->chain);
	free(w);
    }
}

static void
hull_windings_r(hull_t *hull, dclipnode_t *node, struct list_node *polys);

static void
do_hull_recursion(hull_t *hull, dclipnode_t *node, int side,
		  struct list_node *polys)
{
    dclipnode_t *child;
    winding_t *w, *next;

    if (node->children[side] >= 0) {
	child = hull->clipnodes + node->children[side];
	push_node(node, side);
	hull_windings_r(hull, child, polys);
	pop_node();
    } else {
	switch (node->children[side]) {
	case CONTENTS_EMPTY:
	case CONTENTS_WATER:
	case CONTENTS_SLIME:
	case CONTENTS_LAVA:
	    list_for_each_entry_safe(w, next, polys, chain) {
		list_del(&w->chain);
		list_add(&w->chain, &hull_polys);
	    }
	    break;
	case CONTENTS_SOLID:
	case CONTENTS_SKY:
	    /* Throw away polys... */
	    list_for_each_entry_safe(w, next, polys, chain) {
		if (w->pair)
		    w->pair->pair = NULL;
		list_del(&w->chain);
		free(w);
		num_hull_polys--;
	    }
	    break;
	default:
	    Sys_Error("%s: bad contents: %i\n", __func__,
		      node->children[side]);
	    break;
	}
    }
}

static void
hull_windings_r(hull_t *hull, dclipnode_t *node, struct list_node *polys)
{
    mplane_t *plane = hull->planes + node->planenum;
    winding_t *w, *next, *front, *back;
    int i;
    struct list_node frontlist = LIST_HEAD_INIT(frontlist);
    struct list_node backlist = LIST_HEAD_INIT(backlist);

    list_for_each_entry_safe(w, next, polys, chain) {

	/* PARANIOA - PAIR CHECK */
	assert(!w->pair || w->pair->pair == w);

	list_del(&w->chain);
	winding_split(w, plane, &front, &back);
	if (front)
	    list_add(&front->chain, &frontlist);
	if (back)
	    list_add(&back->chain, &backlist);

	if (front && back) {
	    if (w->pair) {
		/* Split the paired poly, preserve pairing */
		winding_t *front2, *back2;
		winding_split(w->pair, plane, &front2, &back2);

		front2->pair = front;
		front->pair = front2;
		back2->pair = back;
		back->pair = back2;

		list_add(&front2->chain, &w->pair->chain);
		list_add(&back2->chain, &w->pair->chain);
		list_del(&w->pair->chain);
		free(w->pair);
		num_hull_polys++;
	    } else {
		front->pair = NULL;
		back->pair = NULL;
	    }
	    free(w);
	    num_hull_polys++;
	}
    }

    w = winding_for_plane(plane);
    if (!w)
	Sys_Error("%s: No winding for plane!\n", __func__);

    for (i = 0; w && i < node_stack_depth; i++) {
	mplane_t *p = hull->planes + node_stack[i]->planenum;
	w = winding_clip(w, p, true, side_stack[i], 0.0001 /* ON_EPSILON */);
    }
    if (w) {
	winding_t *tmp = winding_copy(w);
	winding_reverse(tmp);

	w->pair = tmp;
	tmp->pair = w;

	list_add(&w->chain, &frontlist);
	list_add(&tmp->chain, &backlist);

	/* PARANIOA - PAIR CHECK */
	assert(!w->pair || w->pair->pair == w);

	num_hull_polys += 2;
    } else {
	/* FIXME: fail more gracefully */
	Sys_Error("%s: winding unexpectedly clipped away!\n", __func__);
    }

    do_hull_recursion(hull, node, 0, &frontlist);
    do_hull_recursion(hull, node, 1, &backlist);
}

static void
remove_paired_polys(void)
{
    winding_t *w, *next;

    list_for_each_entry_safe(w, next, &hull_polys, chain) {
	if (w->pair) {
	    list_del(&w->chain);
	    free(w);
	    num_hull_polys--;
	}
    }
}

static void
make_hull_windings(hull_t *hull)
{
    float t1, t2;
    struct list_node head = LIST_HEAD_INIT(head);

    Con_DPrintf("%i clipnodes...\n", hull->lastclipnode - hull->firstclipnode);

    t1 = Sys_DoubleTime();

    /*
     * FIXME(s):
     * - Make sure a map is loaded
     * - Reset cvar to zero and flush data on map load (unload?)
     */
    num_hull_polys = 0;
    node_stack_depth = 0;
    hull_windings_r(hull, hull->clipnodes + hull->firstclipnode, &head);
    remove_paired_polys();

    t2 = Sys_DoubleTime();

    Con_DPrintf("Generated %u polys in %f seconds.\n", num_hull_polys,
		t2 - t1);
}

static void
_gl_drawhull_callback(cvar_t *var)
{
    unsigned val = var->value;

    switch (val) {
    case 0:
	free_hull_polys();
	break;
    case 1:
    case 2:
	//dump_nodes_stderr(&cl.worldmodel->hulls[val]);
	Con_Printf("Generating polygons for hull %u...\n", val);
	free_hull_polys();
	make_hull_windings(&cl.worldmodel->hulls[val]);
	break;
    default:
	Con_Printf("Only values 0, 1, 2 are valid.\n");
	break;
    }
}


cvar_t _gl_drawhull = {
    .name = "_gl_drawhull",
    .string = "0",
    .callback = _gl_drawhull_callback,
    .flags = CVAR_DEVELOPER
};


void
R_DrawWorldHull(void)
{
    winding_t *poly;
    int i;

    list_for_each_entry(poly, &hull_polys, chain) {
	srand((unsigned long)poly);
	glColor3f(rand() % 256 / 255.0, rand() % 256 / 255.0,
		  rand() % 256 / 255.0);
	glBegin(GL_POLYGON);
	for (i = 0; i < poly->numpoints; i++)
	    glVertex3fv(poly->points[i]);
	glEnd();
    }
}
