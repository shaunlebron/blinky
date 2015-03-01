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
// r_misc.c

#include "cmd.h"
#include "console.h"
#include "glquake.h"
#include "protocol.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sys.h"

// FIXME - should only be needed in r_part.c or here, not both.
GLuint particletexture;

/*
==================
R_InitTextures
==================
*/
void
R_InitTextures(void)
{
    int x, y, m;
    byte *dest;

// create a simple checkerboard texture for the default
    r_notexture_mip =
	Hunk_AllocName(sizeof(texture_t) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2,
		       "notexture");

    r_notexture_mip->width = r_notexture_mip->height = 16;
    r_notexture_mip->offsets[0] = sizeof(texture_t);
    r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16 * 16;
    r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8 * 8;
    r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4 * 4;

    for (m = 0; m < 4; m++) {
	dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
	for (y = 0; y < (16 >> m); y++) {
	    for (x = 0; x < (16 >> m); x++) {
		if ((y < (8 >> m)) ^ (x < (8 >> m)))
		    *dest++ = 0;
		else
		    *dest++ = 0xff;
	    }
	}
    }
}

static const byte dottexture[8][8] = {
    {0, 1, 1, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
};

static void
R_InitParticleTexture(void)
{
    int x, y;
    byte data[8][8][4];

    //
    // particle texture
    //
    glGenTextures(1, &particletexture);
    GL_Bind(particletexture);

    for (x = 0; x < 8; x++) {
	for (y = 0; y < 8; y++) {
	    data[y][x][0] = 255;
	    data[y][x][1] = 255;
	    data[y][x][2] = 255;
	    data[y][x][3] = dottexture[x][y] * 255;
	}
    }
    glTexImage2D(GL_TEXTURE_2D, 0, gl_alpha_format, 8, 8, 0, GL_RGBA,
		 GL_UNSIGNED_BYTE, data);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/*
===============
R_Envmap_f

Grab six views for environment mapping tests
===============
*/
static void
R_Envmap_f(void)
{
    byte buffer[256 * 256 * 4];

    glDrawBuffer(GL_FRONT);
    glReadBuffer(GL_FRONT);
    envmap = true;

    r_refdef.vrect.x = 0;
    r_refdef.vrect.y = 0;
    r_refdef.vrect.width = 256;
    r_refdef.vrect.height = 256;

    r_refdef.viewangles[0] = 0;
    r_refdef.viewangles[1] = 0;
    r_refdef.viewangles[2] = 0;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env0.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[1] = 90;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env1.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[1] = 180;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env2.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[1] = 270;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env3.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[0] = -90;
    r_refdef.viewangles[1] = 0;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env4.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[0] = 90;
    r_refdef.viewangles[1] = 0;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env5.rgb", buffer, sizeof(buffer));

    envmap = false;
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    GL_EndRendering();
}

// FIXME - locate somewhere else?
cvar_t r_lockpvs = { "r_lockpvs", "0" };
cvar_t r_lockfrustum = { "r_lockfrustum", "0" };
cvar_t r_drawflat = { "r_drawflat", "0" };

/*
===============
R_Init
===============
*/
void
R_Init(void)
{
    Cmd_AddCommand("envmap", R_Envmap_f);
    Cmd_AddCommand("pointfile", R_ReadPointFile_f);
    Cmd_AddCommand("timerefresh", R_TimeRefresh_f);

    Cvar_RegisterVariable(&r_speeds);
    Cvar_RegisterVariable(&r_fullbright);
    Cvar_RegisterVariable(&r_drawentities);
    Cvar_RegisterVariable(&r_drawviewmodel);
    Cvar_RegisterVariable(&r_drawflat);
#ifdef NQ_HACK
    Cvar_RegisterVariable(&r_lerpmodels);
    Cvar_RegisterVariable(&r_lerpmove);
#endif
    Cvar_RegisterVariable(&r_lockpvs);
    Cvar_RegisterVariable(&r_lockfrustum);

    Cvar_RegisterVariable(&r_norefresh);
    Cvar_RegisterVariable(&r_lightmap);
    Cvar_RegisterVariable(&r_shadows);
    Cvar_RegisterVariable(&r_mirroralpha);
    Cvar_RegisterVariable(&r_wateralpha);
    Cvar_RegisterVariable(&r_dynamic);
    Cvar_RegisterVariable(&r_novis);
    Cvar_RegisterVariable(&r_waterwarp);

    Cvar_RegisterVariable(&gl_finish);
    Cvar_RegisterVariable(&gl_clear);
    Cvar_RegisterVariable(&gl_texsort);

    Cvar_RegisterVariable(&_gl_allowgammafallback);
    Cvar_RegisterVariable(&_gl_drawhull);

    Cvar_RegisterVariable(&gl_cull);
    Cvar_RegisterVariable(&gl_smoothmodels);
    Cvar_RegisterVariable(&gl_affinemodels);
    Cvar_RegisterVariable(&gl_polyblend);
    Cvar_RegisterVariable(&gl_flashblend);
    Cvar_RegisterVariable(&gl_playermip);
    Cvar_RegisterVariable(&gl_nocolors);
    Cvar_RegisterVariable(&gl_zfix);

    Cvar_RegisterVariable(&gl_keeptjunctions);
    Cvar_RegisterVariable(&gl_reporttjunctions);

#ifdef NQ_HACK
    Cvar_RegisterVariable(&gl_doubleeyes);
#endif
#ifdef QW_HACK
    Cvar_RegisterVariable(&r_netgraph);
#endif

    R_InitBubble();

    R_InitParticles();
    R_InitParticleTexture();

#ifdef QW_HACK
    glGenTextures(1, &netgraphtexture);
#endif
    glGenTextures(MAX_CLIENTS, playertextures);
}


/*
===============
R_SetVrect
===============
*/
void
R_SetVrect(const vrect_t *pvrectin, vrect_t *pvrect, int lineadj)
{
    int h;
    float size;
    qboolean full;

#ifdef NQ_HACK
    full = (scr_viewsize.value >= 120.0f);
#endif
#ifdef QW_HACK
    full = (!cl_sbar.value && scr_viewsize.value >= 100.0f);
#endif
    size = qmin(scr_viewsize.value, 100.0f);

    /* Hide the status bar during intermission */
    if (cl.intermission) {
	full = true;
	size = 100.0;
	lineadj = 0;
    }
    size /= 100.0;

    if (full)
	h = pvrectin->height;
    else
	h = pvrectin->height - lineadj;

    pvrect->width = pvrectin->width * size;
    if (pvrect->width < 96) {
	size = 96.0 / pvrectin->width;
	pvrect->width = 96;	// min for icons
    }
    //pvrect->width &= ~7;

    pvrect->height = pvrectin->height * size;
    if (!full) {
	if (pvrect->height > pvrectin->height - lineadj)
	    pvrect->height = pvrectin->height - lineadj;
    } else if (pvrect->height > pvrectin->height)
	pvrect->height = pvrectin->height;
    //pvrect->height &= ~1;

    pvrect->x = (pvrectin->width - pvrect->width) / 2;
    if (full)
	pvrect->y = 0;
    else
	pvrect->y = (h - pvrect->height) / 2;
}

/*
 * ================
 * ResampleXlate
 * ================
 * Resample the source texture while applying colour translation
 *
 * The input texture may be a sub-rectangle (assumed to be left aligned) so
 * input stride is specified separately from input width.
 */
static void
ResampleXlate(const byte *in,
	      unsigned inwidth, unsigned inheight, unsigned instride,
	      unsigned *out,
	      unsigned outwidth, unsigned outheight,
	      const unsigned *xlate)
{
    int i, j;
    const byte *inrow;
    unsigned frac, fracstep;

    fracstep = inwidth * 0x10000 / outwidth;
    for (i = 0; i < outheight; i++, out += outwidth) {
	inrow = in + instride * (i * inheight / outheight);
	frac = fracstep >> 1;
	for (j = 0; j < outwidth; j += 4) {
	    out[j] = xlate[inrow[frac >> 16]];
	    frac += fracstep;
	    out[j + 1] = xlate[inrow[frac >> 16]];
	    frac += fracstep;
	    out[j + 2] = xlate[inrow[frac >> 16]];
	    frac += fracstep;
	    out[j + 3] = xlate[inrow[frac >> 16]];
	    frac += fracstep;
	}
    }
}

/*
===============
R_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/
void
R_TranslatePlayerSkin(int playernum)
{
    int top, bottom;
    unsigned translate[256];
    int i;
    const byte *original;
    unsigned pixels[512 * 256];
    unsigned scaled_width, scaled_height;
    int inwidth, inheight, instride;
    player_info_t *player;

#ifdef QW_HACK
    const char *skin_key;
    char skin[MAX_QPATH];
#endif

    GL_DisableMultitexture();

    /*
     * Determin top and bottom colours
     */
    player = &cl.players[playernum];
#ifdef QW_HACK
    if (!player->name[0])
	return;

    skin_key = Info_ValueForKey(player->userinfo, "skin");
    COM_StripExtension(skin_key, skin, sizeof(skin));
    if (player->skin && !strcasecmp(skin, player->skin->name))
	player->skin = NULL;

    if (player->_topcolor == player->topcolor &&
	player->_bottomcolor == player->bottomcolor && player->skin)
	return;

    player->_topcolor = player->topcolor;
    player->_bottomcolor = player->bottomcolor;
#endif
    top = qclamp((int)player->topcolor, 0, 13) * 16;
    bottom = qclamp((int)player->bottomcolor, 0, 13) * 16;

    for (i = 0; i < 256; i++)
	translate[i] = d_8to24table[i];

    for (i = 0; i < 16; i++) {
	/* the artists made some backwards ranges */
	if (top < 128)
	    translate[TOP_RANGE + i] = d_8to24table[top + i];
	else
	    translate[TOP_RANGE + i] = d_8to24table[top + 15 - i];

	if (bottom < 128)
	    translate[BOTTOM_RANGE + i] = d_8to24table[bottom + i];
	else
	    translate[BOTTOM_RANGE + i] = d_8to24table[bottom + 15 - i];
    }

    /*
     * Locate the original skin pixels
     */
#ifdef NQ_HACK
    entity_t *entity = &cl_entities[1 + playernum];
    model_t *model = entity->model;
    const aliashdr_t *aliashdr;

    if (!model)
	return;			// player doesn't have a model yet
    if (model->type != mod_alias)
	return;			// only translate skins on alias models

    aliashdr = Mod_Extradata(model);
    original = (const byte *)aliashdr + aliashdr->skindata;
    if (entity->skinnum < 0 || entity->skinnum >= aliashdr->numskins) {
	Con_DPrintf("Player %d has invalid skin #%d\n",
		    playernum, entity->skinnum);
    } else {
	const int skinsize = aliashdr->skinwidth * aliashdr->skinheight;
	if (skinsize & 3)
	    Sys_Error("%s: skinsize & 3", __func__);
	original += entity->skinnum * skinsize;
    }

    inwidth = instride = aliashdr->skinwidth;
    inheight = aliashdr->skinheight;
#endif
#ifdef QW_HACK
    /* Hard coded width from original model */
    inwidth = 296;
    inheight = 194;

    if (!player->skin)
	Skin_Find(player);
    if ((original = Skin_Cache(player->skin)) != NULL) {
	/* Skin data width for custom skins */
	instride = 320;
    } else {
	model_t *model = cl.model_precache[cl_playerindex];
	const aliashdr_t *aliashdr = Mod_Extradata(model);
	original = (const byte *)aliashdr + aliashdr->skindata;
	instride = inwidth;
    }
#endif

    // because this happens during gameplay, do it fast
    // instead of sending it through gl_upload 8
    GL_Bind(playertextures[playernum]);

    // allow users to crunch sizes down
    scaled_width = 512 >> (int)gl_playermip.value;
    scaled_height = 256 >> (int)gl_playermip.value;

    // make sure not still too big
    scaled_width = qmin((unsigned)gl_max_size.value, scaled_width);
    scaled_height = qmin((unsigned)gl_max_size.value, scaled_height);

    ResampleXlate(original, inwidth, inheight, instride,
		  pixels, scaled_width, scaled_height, translate);
    glTexImage2D(GL_TEXTURE_2D, 0, gl_solid_format, scaled_width,
		 scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}


/*
===============
R_NewMap
===============
*/
void
R_NewMap(void)
{
    void *hunkbase;
    int i;

    for (i = 0; i < 256; i++)
	d_lightstylevalue[i] = 264;	// normal light value

    memset(&r_worldentity, 0, sizeof(r_worldentity));
    r_worldentity.model = &cl.worldmodel->model;

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
    for (i = 0; i < cl.worldmodel->numleafs; i++)
	cl.worldmodel->leafs[i].efrags = NULL;

    r_viewleaf = NULL;
    R_ClearParticles();

    hunkbase = Hunk_AllocName(0, "gl_polys");
    GL_BuildLightmaps(hunkbase);

    /* identify mirror texture */
    mirrortexturenum = -1;
    for (i = 0; i < cl.worldmodel->numtextures; i++) {
	if (!cl.worldmodel->textures[i])
	    continue;
	if (!strncmp(cl.worldmodel->textures[i]->name, "window02_1", 10))
	    mirrortexturenum = i;
	cl.worldmodel->textures[i]->texturechain = NULL;
    }
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void
R_TimeRefresh_f(void)
{
    int i;
    float start, stop, time;

    glDrawBuffer(GL_FRONT);
    glFinish();

    start = Sys_DoubleTime();
    for (i = 0; i < 128; i++) {
	r_refdef.viewangles[1] = i / 128.0 * 360.0;
	R_RenderView();
    }

    glFinish();
    stop = Sys_DoubleTime();
    time = stop - start;
    Con_Printf("%f seconds (%f fps)\n", time, 128 / time);

    glDrawBuffer(GL_BACK);
    GL_EndRendering();
}

void
D_FlushCaches(void)
{
}
