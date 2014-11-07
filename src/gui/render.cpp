#include "vDos.h"
#include "video.h"
#include "render.h"
#include "mem.h"

Render_t render;
ScalerLineHandler_t RENDER_DrawLine;

Bit8u rendererCache[RENDER_MAXHEIGHT * RENDER_MAXWIDTH];

Bit16u curAttrChar[txtMaxLins*txtMaxCols];											// Current displayed textpage
Bit16u *newAttrChar;																// To be replaced by

void RENDER_ForceUpdate()
	{
	if (newAttrChar)
		for (int i = ttf.cols*ttf.lins-1; i >= 0; i--)								// Force text screen to redraw
			curAttrChar[i] = newAttrChar[i]-1;
	}

void SimpleRenderer(const void *s)
	{
	render.cache.curr_y++;
	if (render.cache.invalid || memcmp(render.cache.pointer, s, render.cache.width))
		{
		memcpy(render.cache.pointer, s, render.cache.width);
		render.cache.past_y = render.cache.curr_y;
		}
	render.cache.pointer += render.cache.width;
	}

static void RENDER_EmptyLineHandler(const void * src)
	{
	}

static void RENDER_StartLineHandler(const void * s)
	{
	if (render.cache.invalid || memcmp(render.cache.pointer, s, render.cache.width))
		{
		if (!GFX_StartUpdate())
			{
			RENDER_DrawLine = RENDER_EmptyLineHandler;
			return;
			}
		render.cache.start_y = render.cache.past_y = render.cache.curr_y;
		RENDER_DrawLine = SimpleRenderer;
		RENDER_DrawLine(s);
		return;
		}
	render.cache.pointer += render.cache.width;
	render.cache.curr_y++;
	}

bool RENDER_StartUpdate(void)
	{
	if (!render.active)
		return false;

	render.cache.pointer = (Bit8u*)&rendererCache;
	render.cache.start_y = render.cache.past_y = render.cache.curr_y = 0;

	if (render.cache.nextInvalid)													// Always do a full screen update
		{
		render.cache.nextInvalid = false;
		render.cache.invalid = true;
		if (!GFX_StartUpdate())
			return false;
		RENDER_DrawLine = SimpleRenderer;
		}
	else
		RENDER_DrawLine = RENDER_StartLineHandler;
	return true;
	}

void RENDER_EndUpdate()
	{
	RENDER_DrawLine = RENDER_EmptyLineHandler;
	if (render.cache.start_y != render.cache.past_y)
		GFX_EndUpdate();
	render.cache.invalid = false;
	}

void RENDER_SetSize(Bitu width, Bitu height)
	{
	render.cache.width	= width;
	render.cache.height	= height;
	GFX_SetSize();																	// Setup the scaler variables
	render.cache.nextInvalid = true;												// Signal the next frame to first reinit the cache
	render.active = true;
	}

void RENDER_Init()
	{
	}

