#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include "SDL.h"
#include "..\..\SDL-1.2.15\include\SDL_syswm.h"
#include "sdl_ttf.h"
#include "vDos.h"
#include "video.h"
#include "mouse.h"
#include "bios.h"
#include "vDosTTF.h"
#include "render.h"
#include "vga.h"
#include "..\ints\int10.h"
#include "dos_inc.h"
#include "mem.h"
#include "mouse.h"
#include <Shellapi.h>

#define WM_SC_SYSMENUABOUT		0x1110												// Pretty arbitrary, just needs to be < 0xF000
#define WM_SC_SYSMENUPCOPY		(WM_SC_SYSMENUABOUT + 1)
#define WM_SC_SYSMENUPASTE		(WM_SC_SYSMENUABOUT + 2)
#define WM_SC_SYSMENUDECREASE	(WM_SC_SYSMENUABOUT + 3)
#define WM_SC_SYSMENUINCREASE	(WM_SC_SYSMENUABOUT + 4)

#define aboutWidth 400
#define aboutHeight 160

typedef struct {
	Bit8u blue;
	Bit8u green;
	Bit8u red;
	Bit8u alpha;		// unused
} alt_rgb;

alt_rgb altBGR0[16], altBGR1[16];


struct SDL_Block {
	bool	active;																	// If this isn't set don't draw
	short	scale;
	Bit16u	width;
	Bit16u	height;
	SDL_Surface * surface;
};
SDL_Block sdl;

HWND	sdlHwnd;

static DWORD ttfSize= sizeof(vDosTTFbi);
static void * ttfFont = vDosTTFbi;
static bool winFramed;
static bool screendump = false;
static bool fontBoxed = false;														// ASCII 176-223 box/lines or extended ASCII
static int prevPointSize = 0;
static RECT prevPosition;
static int winPerc = 75;
static int initialX = -1;
static int initialY = -1;

bool aboutShown = false;
bool selectingText = false;
int selStartX, selPosX1, selPosX2, selStartY, selPosY1, selPosY2;

Render_ttf ttf;

static SDL_Color ttf_fgColor = {0, 0, 0, 0}; 
static SDL_Color ttf_bgColor = {0, 0, 0, 0};
static SDL_Rect ttf_textRect = {0, 0, 0, 0};
static SDL_Rect ttf_textClip = {0, 0, 0, 0};

static WNDPROC fnSDLDefaultWndProc;

static int fullLeftOff, fullTopOff;


static void TimedSetSize()
	{
	if (vga.mode == M_TEXT && ttf.fullScrn)
		{
		fullLeftOff = GetSystemMetrics(SM_CXSCREEN);
		fullTopOff = GetSystemMetrics(SM_CYSCREEN);
		sdl.surface = SDL_SetVideoMode(fullLeftOff, fullTopOff, 32, SDL_SWSURFACE|SDL_NOFRAME);
		fullLeftOff = (fullLeftOff-sdl.width)/2;
		fullTopOff = (fullTopOff-sdl.height)/2;
		}
	else
		sdl.surface = SDL_SetVideoMode(sdl.width, sdl.height, 32, SDL_SWSURFACE|(winFramed ? 0 : SDL_NOFRAME));
	if (!sdl.surface)
		E_Exit("SDL: Failed to create surface");
	if (initialX >= 0)																// Position window (only once at startup)
		{
		RECT SDLRect;
		GetWindowRect(sdlHwnd, &SDLRect);											// Window has to be shown completely
		if (initialX+SDLRect.right-SDLRect.left <= GetSystemMetrics(SM_CXSCREEN) && initialY+SDLRect.bottom-SDLRect.top <= GetSystemMetrics(SM_CYSCREEN))
			MoveWindow(sdlHwnd, initialX, initialY, SDLRect.right - SDLRect.left, SDLRect.bottom - SDLRect.top, true);
		initialX = -1;
		}
	SDL_ShowCursor(!mouseWP6x && (!ttf.fullScrn || usesMouse));
	sdl.active = true;
	}

void GFX_SetSize()
	{
	if (vga.mode == M_TEXT)
		{
		sdl.width = ttf.cols*ttf.width;
		sdl.height = ttf.lins*ttf.height;
		}
	else
		{
		sdl.width = render.cache.width * sdl.scale;
		sdl.height = render.cache.height * sdl.scale;
		}
	if (!winHidden)
		TimedSetSize();
	}

bool GFX_StartUpdate()
	{
	if (winHidden && GetTickCount() >= hideWinTill)
		{
		winHidden = false;
		TimedSetSize();
		}
	return sdl.active;
	}

int GFX_SetCodePage(int cp)
	{
	unsigned char cTest[256];														// ASCII format
	for (int i = 0; i < 256; i++)
		cTest[i] = i;
	Bit16u wcTest[256];
	if (MultiByteToWideChar(cp, 0, (char*)cTest, 256, NULL, 0) == 256)
		{
		int notMapped = 0;															// Number of characters not defined in font
		MultiByteToWideChar(cp, 0, (char*)cTest, 256, (LPWSTR)wcTest, 256);
		Bit16u unimap;	
		for (int c = 128+(TTF_GlyphIsProvided(ttf.SDL_font, 0x20ac) ? 1 : 0); c < 256; c++)	// Check all characters above 128 (128 = always Euro symbol if defined in font)
			{
			unimap = wcTest[c];
			if (!fontBoxed || c < 176 || c > 223)
				{
				if (!TTF_GlyphIsProvided(ttf.SDL_font, unimap))
					notMapped++;
				else 
					cpMap[c] = unimap;
				}
			}
		return notMapped;
		}
	return -1;																		// Code page not set
	}

void GFX_SelectFontByPoints(int ptsize)
	{
	if (ttf.SDL_font != 0)
		TTF_CloseFont(ttf.SDL_font);
	SDL_RWops *rwfont = SDL_RWFromConstMem(ttfFont, (int)ttfSize);
	ttf.SDL_font = TTF_OpenFontRW(rwfont, 1, ptsize);

	ttf.pointsize = ptsize;
	TTF_GlyphMetrics(ttf.SDL_font, 65, NULL, NULL, NULL, NULL, &ttf.width);
	ttf.height = TTF_FontAscent(ttf.SDL_font)-TTF_FontDescent(ttf.SDL_font);
	if (ttf.fullScrn)
		{
		ttf.offX = (GetSystemMetrics(SM_CXSCREEN)-ttf.width*ttf.cols)/2;
		ttf.offY = (GetSystemMetrics(SM_CYSCREEN)-ttf.height*ttf.lins)/2;
		}
	else
		ttf.offX = ttf.offY = 0;
	if (!codepage)
		{
		int cp = GetOEMCP();
		codepage = GFX_SetCodePage(cp) == -1 ? 437 : cp;
		}
	}


static alt_rgb *rgbColors = (alt_rgb*)render.pal.rgb;

static int prev_sline = -1;
static bool hasFocus = true;														// Only used if not framed
static bool hasMinButton = false;													// ,,
static bool cursor_enabled = false;

bool dumpScreen(void)
	{
	if (vga.mode != M_TEXT)
		return false;
	FILE * fp;
	if (!(fp = fopen("screen.txt", "wb")))
		return false;

	Bit16u textLine[txtMaxCols+1];
	Bit16u *curAC = curAttrChar;
	fprintf(fp, "\xff\xfe");														// It's a Unicode text file
	for (int lineno = 0; lineno < ttf.lins; lineno++)
		{
		int chars = 0;
		for (int xPos = 0; xPos < ttf.cols; xPos++)
			{
			Bit8u textChar =  *(curAC++)&255;
			textLine[xPos] = cpMap[textChar];
			if (textChar != 32)
				chars = xPos+1;
			}
		if (chars)																	// If not blank line
			fwrite(textLine, 2, chars, fp);											// Write truncated
		if (lineno < ttf.lins-1)													// Append linefeed for all but the last line
			fwrite("\x0d\x00\x0a\x00", 1, 4, fp);
		}
	fclose(fp);
	ShellExecute(NULL, "open", "screen.txt", NULL, NULL, SW_SHOWNORMAL);			// Open text file
	return true;
	}

void getClipboard(void)
	{
	if (OpenClipboard(NULL))
		{	
		if (HANDLE cbText = GetClipboardData(CF_UNICODETEXT))
			{
			Bit16u *p = (Bit16u *)GlobalLock(cbText);
			BIOS_PasteClipboard(p);
			GlobalUnlock(cbText);
			}
		CloseClipboard();
		}
	}

void GFX_EndTextLines(void)
	{
	if (aboutShown || selectingText)												// The easy way: don't update if About box or selectting text
		return;
	Uint16 unimap[txtMaxCols+1];													// Max+1 charaters in a line
	int xmin = ttf.cols;															// Keep track of changed area
	int ymin = ttf.lins;
	int xmax = -1;
	int ymax = -1;
	Bit16u *curAC = curAttrChar;													// Old/current buffer
	Bit16u *newAC = newAttrChar;													// New/changed buffer

	if (ttf.cursor >= 0 && ttf.cursor < ttf.cols*ttf.lins)							// Hide/restore (previous) cursor-character if we had one
//		if (ttf.cursor != vga.draw.cursor.address>>1 || (vga.draw.cursor.enabled !=  cursor_enabled) || vga.draw.cursor.sline > vga.draw.cursor.eline || vga.draw.cursor.sline > 15)
		if (ttf.cursor != vga.draw.cursor.address>>1 || vga.draw.cursor.sline > vga.draw.cursor.eline || vga.draw.cursor.sline > 15)
			curAC[ttf.cursor] = newAC[ttf.cursor]^0xf0f0;							// Force redraw (differs)

	ttf_textClip.h = ttf.height;
	ttf_textClip.y = 0;
	for (int y = 0; y < ttf.lins; y++)
		{
		if (!hasFocus)
			if (y == 0)																// Dim topmost line
				rgbColors = altBGR0;
			else if (y == 1)
				rgbColors = altBGR1;												// Undim the rest

		ttf_textRect.y = ttf.offY+y*ttf.height;
		for (int x = 0; x < ttf.cols; x++)
			{
			if (newAC[x] != curAC[x])
				{
				xmin = min(x, xmin);
				ymin = min(y, ymin);
				ymax = y;
				ttf_textRect.x = ttf.offX+x*ttf.width;

				Bit8u colorBG = newAC[x]>>12;
				Bit8u colorFG = (newAC[x]>>8)&15;
				if (CurMode->mode == 7)												// Mono (Hercules)
					{
					TTF_SetFontStyle(ttf.SDL_font, (colorFG&7) == 1 ? TTF_STYLE_UNDERLINE : TTF_STYLE_NORMAL);
					if ((colorFG&0xa) == colorFG && colorBG == 0)
						colorFG = 8;
					else if (colorFG&7)
						colorFG |= 7;
					}
				else if (wpVersion > 0)												// If WP and not negative (color value to text attribute excluded)
					{
					if (colorFG == 0xe && colorBG == 1)
						{
						TTF_SetFontStyle(ttf.SDL_font, TTF_STYLE_ITALIC);
						colorFG = 7;
						}
					else if ((colorFG == 1 || colorFG == 0xf) && colorBG == 7)
						{
						TTF_SetFontStyle(ttf.SDL_font, TTF_STYLE_UNDERLINE);
						colorBG = 1;
						colorFG = colorFG == 1 ? 7 : 0xf;
						}
					else
						TTF_SetFontStyle(ttf.SDL_font, TTF_STYLE_NORMAL);
					}
				else if (wsVersion)													// If WordStar
					{
					int style = 0;
					if (colorBG&8)													// If "blinking" set, modify attributes
						{
						if (colorBG&1)
							style |= TTF_STYLE_UNDERLINE;
						if (colorBG&2)
							style |= TTF_STYLE_ITALIC;
						if (colorBG&4)
							style |= TTF_STYLE_STRIKETHROUGH;
						colorBG = wsBackGround;										// Background color (text) is fixed at this
						}
					TTF_SetFontStyle(ttf.SDL_font, style);
					}
				ttf_bgColor.b = rgbColors[colorBG].blue;
				ttf_bgColor.g = rgbColors[colorBG].green;
 				ttf_bgColor.r = rgbColors[colorBG].red;
				ttf_fgColor.b = rgbColors[colorFG].blue;
				ttf_fgColor.g = rgbColors[colorFG].green;
				ttf_fgColor.r = rgbColors[colorFG].red;

				int x1 = x;
				Bit8u ascii = newAC[x]&255;
				if (ascii > 175 && ascii < 179)										// Special: shade characters 176-178
					{
					ttf_bgColor.b = (ttf_bgColor.b*(179-ascii) + ttf_fgColor.b*(ascii-175))>>2;
					ttf_bgColor.g = (ttf_bgColor.g*(179-ascii) + ttf_fgColor.g*(ascii-175))>>2;
					ttf_bgColor.r = (ttf_bgColor.r*(179-ascii) + ttf_fgColor.r*(ascii-175))>>2;
					do																// As long char and foreground/background color equal
						{
						curAC[x] = newAC[x];
						unimap[x-x1] = ' ';											// Shaded space
						x++;
						}
					while (x < ttf.cols && newAC[x] == newAC[x1] && newAC[x] != curAC[x]);
					}
				else
					{
					Bit8u color = newAC[x]>>8;
					do																// As long foreground/background color equal
						{
						curAC[x] = newAC[x];
						unimap[x-x1] = cpMap[ascii];
						x++;
						ascii = newAC[x]&255;
						}
					while (x < ttf.cols && newAC[x] != curAC[x] && newAC[x]>>8 == color && (ascii < 176 || ascii > 178));
					}
				unimap[x-x1] = 0;
				xmax = max(x-1, xmax);

				SDL_Surface* textSurface = TTF_RenderUNICODE_Shaded(ttf.SDL_font, unimap, ttf_fgColor, ttf_bgColor);
				ttf_textClip.w = (x-x1)*ttf.width;
				SDL_BlitSurface(textSurface, &ttf_textClip, sdl.surface, &ttf_textRect);
				SDL_FreeSurface(textSurface);
				x--;
				}
			}
		curAC += ttf.cols;
		newAC += ttf.cols;
		}
	if (hasFocus && vga.draw.cursor.enabled && vga.draw.cursor.sline <= vga.draw.cursor.eline && vga.draw.cursor.sline < 16)	// Draw cursor?
		{
		int newPos = vga.draw.cursor.address>>1;
		if (newPos >= 0 && newPos < ttf.cols*ttf.lins)								// If on screen
			{
			int y = newPos/ttf.cols;
			int x = newPos%ttf.cols;
			if (ttf.cursor != newPos || vga.draw.cursor.sline != prev_sline)		// If new position or shape changed, forse draw
				{
				prev_sline = vga.draw.cursor.sline;
				xmin = min(x, xmin);
				xmax = max(x, xmax);
				ymin = min(y, ymin);
				ymax = max(y, ymax);
				}
			ttf.cursor = newPos;
			if (x >= xmin && x <= xmax && y >= ymin && y <= ymax)					// If overdrawn previuosly (or new shape)
				{
				Bit8u colorBG = newAttrChar[ttf.cursor]>>12;
				Bit8u colorFG = (newAttrChar[ttf.cursor]>>8)&15;
				if (wpVersion > 0)													// If WP and not negative (color value to text attribute excluded)
					{
					if (colorFG == 0xe && colorBG == 1)
						{
						TTF_SetFontStyle(ttf.SDL_font, TTF_STYLE_ITALIC);
						colorFG = 7;
						}
					else if ((colorFG == 1 || colorFG == 0xf) && colorBG == 7)
						{
						TTF_SetFontStyle(ttf.SDL_font, TTF_STYLE_UNDERLINE);
						colorBG = 1;
						colorFG = colorFG == 1 ? 7 : 0xf;
						}
					else
						TTF_SetFontStyle(ttf.SDL_font, TTF_STYLE_NORMAL);
					}
				ttf_bgColor.b = rgbColors[colorBG].blue;
				ttf_bgColor.g = rgbColors[colorBG].green;
				ttf_bgColor.r = rgbColors[colorBG].red;
				ttf_fgColor.b = rgbColors[colorFG].blue;
				ttf_fgColor.g = rgbColors[colorFG].green;
				ttf_fgColor.r = rgbColors[colorFG].red;
				unimap[0] = cpMap[newAttrChar[ttf.cursor]&255];
				unimap[1] = 0;
				// first redraw character
				SDL_Surface* textSurface = TTF_RenderUNICODE_Shaded(ttf.SDL_font, unimap, ttf_fgColor, ttf_bgColor);
				ttf_textClip.w = ttf.width;
				ttf_textRect.x = ttf.offX+x*ttf.width;
				ttf_textRect.y = ttf.offY+y*ttf.height;
				SDL_BlitSurface(textSurface, &ttf_textClip, sdl.surface, &ttf_textRect);
				SDL_FreeSurface(textSurface);
				// seccond reverse lower lines
				textSurface = TTF_RenderUNICODE_Shaded(ttf.SDL_font, unimap, ttf_bgColor, ttf_fgColor);
				ttf_textClip.y = (ttf.height*vga.draw.cursor.sline)>>4;
				ttf_textClip.h = ttf.height - ttf_textClip.y;						// For now, cursor to bottom
				ttf_textRect.y = ttf.offY+y*ttf.height + ttf_textClip.y;
				SDL_BlitSurface(textSurface, &ttf_textClip, sdl.surface, &ttf_textRect);
				SDL_FreeSurface(textSurface);
				}
			}
		}

	if ((!hasFocus || hasMinButton) && !ttf.fullScrn && xmax == ttf.cols-1 && ymin == 0)	// (Re)draw Minimize button?
		{
		Bit8u color = newAttrChar[xmax]>>12;
		ttf_fgColor.b = rgbColors[color].blue;
		ttf_fgColor.g = rgbColors[color].green;
		ttf_fgColor.r = rgbColors[color].red;
		color = (newAttrChar[xmax]>>8)&15;
		ttf_bgColor.b = rgbColors[color].blue;
		ttf_bgColor.g = rgbColors[color].green;
		ttf_bgColor.r = rgbColors[color].red;
		unimap[0] = cpMap[45];														// '-'
		unimap[1] = 0;
		SDL_Surface* textSurface = TTF_RenderUNICODE_Shaded(ttf.SDL_font, unimap, ttf_fgColor, ttf_bgColor);
		ttf_textClip.w = ttf.width;
		ttf_textClip.y = ttf.height>>2;
		ttf_textClip.h = ttf.height>>1;
		ttf_textRect.x = ttf.offX+xmax*ttf.width;
		ttf_textRect.y = ttf.offY;
		SDL_BlitSurface(textSurface, &ttf_textClip, sdl.surface, &ttf_textRect);
		SDL_FreeSurface(textSurface);
		}
	if (xmin <= xmax)																// If any changes
		SDL_UpdateRect(sdl.surface, ttf.offX+xmin*ttf.width, ttf.offY+ymin*ttf.height, (xmax-xmin+1)*ttf.width, (ymax-ymin+1)*ttf.height);
	}

void GFX_EndUpdate(void)
	{
	if (vga.mode == M_TEXT)
		GFX_EndTextLines();
	else
		{
		int pixs = sdl.surface->w/render.cache.width;								// Parachute/more safe than sdl.scale???
		Bit32u *pointer = (Bit32u *)sdl.surface->pixels + (render.cache.start_y*sdl.surface->w)*pixs;
		Bit8u *cache = rendererCache + render.cache.start_y*render.cache.width;

		for (int i = render.cache.past_y - render.cache.start_y; i; i--)			// Build first to last changed line
			{
			for (int j = render.cache.width; j; j--)								// Build one line
				{
				Bit32u pColor = ((Bit32u *)render.pal.rgb)[*cache++];
				int k = pixs;
				do
					*pointer++ = pColor;
				while (--k);
				}
			for (int j = pixs-1; j; j--)											// Duplicate line pixs
				{
				memcpy(pointer, pointer-sdl.surface->w, render.cache.width*pixs*4);
				pointer += sdl.surface->w;
				}
			}
		SDL_UpdateRect(sdl.surface, 0, render.cache.start_y*pixs, render.cache.width*pixs, (render.cache.past_y - render.cache.start_y)*pixs);
		}
	}

static void decreaseFontSize()
	{
	if (vga.mode == M_TEXT && ttf.pointsize > 12)
		{
		GFX_SelectFontByPoints(ttf.pointsize - (ttf.vDos ? 2 : 1));
		GFX_SetSize();
		memset(curAttrChar, -1, ttf.cols*ttf.lins*2);								// Force redraw of complete window
		if (ttf.fullScrn)															// Smaller content area leaves old one behind
			{
			SDL_FillRect(sdl.surface, NULL, 0);
			SDL_UpdateRect(sdl.surface, 0, 0, 0, 0);
			}
		GFX_EndTextLines();
		}
	}

static void increaseFontSize()
	{
	if (vga.mode == M_TEXT)															// Increase fontsize
		{
		int maxWidth = GetSystemMetrics(SM_CXSCREEN);
		int maxHeight = GetSystemMetrics(SM_CYSCREEN);
		if (!ttf.fullScrn && winFramed)												// 3D borders
			{
			maxWidth -= GetSystemMetrics(SM_CXBORDER)*2;
			maxHeight -= GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYBORDER)*2;
			}
		GFX_SelectFontByPoints(ttf.pointsize + (ttf.vDos ? 2 : 1));
		if (ttf.cols*ttf.width <= maxWidth && ttf.lins*ttf.height <= maxHeight)		// if it fits on screen
			{
			GFX_SetSize();
			memset(curAttrChar, -1, ttf.cols*ttf.lins*2);							// Force redraw of complete window
			GFX_EndTextLines();
			}
		else
			GFX_SelectFontByPoints(ttf.pointsize - (ttf.vDos ? 2 : 1));
		}
	}

void readTTF(const char *fName)														// Open and read alternative font
	{
	FILE * ttf_fh;
	char ttfPath[1024];

	strcpy(ttfPath, fName);															// Try to load it from working directory
	strcat(ttfPath, ".ttf");
	if (!(ttf_fh  = fopen(ttfPath, "rb")))
		{
		strcpy(strrchr(strcpy(ttfPath, _pgmptr), '\\')+1, fName);					// Try to load it from where vDos was started
		strcat(ttfPath, ".ttf");
		ttf_fh  = fopen(ttfPath, "rb");
		}
	if (ttf_fh)
		{
		if (!fseek(ttf_fh, 0, SEEK_END))
			if ((ttfSize = ftell(ttf_fh)) != -1L)
				if (ttfFont = malloc((size_t)ttfSize))
					if (!fseek(ttf_fh, 0, SEEK_SET))
						if (fread(ttfFont, 1, (size_t)ttfSize, ttf_fh) == (size_t)ttfSize)
							{
							fclose(ttf_fh);
							return;
							}
		fclose(ttf_fh);
		}
	ConfAddError("Could not load TTF font file\n", (char *)fName);
	}

static LRESULT CALLBACK SysMenuExtendWndProc(HWND hwnd, UINT uiMsg, WPARAM wparam, LPARAM lparam)
	{
	if (uiMsg  == WM_SYSCOMMAND)
		{
		if (aboutShown || selectingText)											// Selecting text can't be set at this time, but...
			{
			SDL_UpdateRect(sdl.surface, 0, 0, 0, 0);								// Too much and not always needed, so?
			aboutShown = false;
			selectingText = false;
			}
		switch (wparam)
			{
		case WM_SC_SYSMENUABOUT:
			{
			char info[] = "vDos version yyyy.mm.dd - by Jos Schaars,\n\nderived from DOSBox - by the DOSBox team,\n\ntherefore also published under the GNU GPL.";
			strncpy(info+13, vDosVersion, 10);

			HDC hDC = GetDC(sdlHwnd);
			RECT rect;
			GetClientRect(sdlHwnd, &rect);
			rect.top = (rect.bottom - aboutHeight)/2;
			rect.bottom = (rect.bottom + aboutHeight)/2;
			rect.left = (rect.right - aboutWidth)/2;
			rect.right = (rect.right + aboutWidth)/2;
			HBRUSH hBrush = CreateSolidBrush(RGB(224, 224, 224));
			SelectObject(hDC, hBrush);
			HPEN hPen = CreatePen(PS_SOLID, 4, RGB(176, 192, 192));
			SelectObject(hDC, hPen);
			Rectangle(hDC, rect.left, rect.top, rect.right, rect.bottom);
			DeleteObject(hPen);
			DeleteObject(hBrush);

			rect.top += 32;
			HFONT hFont = CreateFont(20, 0, 0, 0, FW_NORMAL, false, false, false, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, "Arial");
			SelectObject(hDC, hFont);
			SetTextColor(hDC, RGB(0, 0, 0));
			SetBkColor(hDC, RGB(224, 224, 224));
			DrawText(hDC, info, -1, &rect, DT_CENTER);
			DeleteObject(hFont);

			ReleaseDC(sdlHwnd, hDC);
			aboutShown = true;
			}
			return 0;
		case WM_SC_SYSMENUPCOPY:
			dumpScreen();
			return 0;
		case WM_SC_SYSMENUPASTE:
			getClipboard();
			return 0;
		case WM_SC_SYSMENUDECREASE:
			decreaseFontSize();
			return 0;
		case WM_SC_SYSMENUINCREASE:
			increaseFontSize();
			return 0;
			}
		}
	return CallWindowProc(fnSDLDefaultWndProc, hwnd, uiMsg, wparam, lparam);
	}

bool getWP(char * wpStr)
	{
	if (!strnicmp(wpStr, "WS", 2))													// WP = WS (WordStar)
		{
		wpStr += 2;
		wsVersion = 1;
		lTrim(wpStr);
		if (*wpStr == 0)
			return true;
		if (sscanf(wpStr, ", %d", &wsBackGround) == 1)
			if (wsBackGround >= 0 && wsBackGround <= 15)
				return true;
		wsBackGround = 0;
		wsVersion = 0;
		return false;
		}
	if (sscanf(wpStr, "%d", &wpVersion) == 1)										// No checking on version number
		return true;
	return false;

	}

bool setColors(const char *colorArray)
	{
	const char * nextRGB = colorArray;
	Bit8u * altPtr = (Bit8u *)altBGR1;
	int rgbVal[3];
	for (int colNo = 0; colNo < 16; colNo++)
		{
		if (sscanf(nextRGB, " ( %d , %d , %d )", &rgbVal[0], &rgbVal[1], &rgbVal[2]) == 3)	// Decimal: (red,green,blue)
			{
			for (int i = 0; i< 3; i++)
				{
				if (rgbVal[i] < 0 || rgbVal[i] >255)
					return false;
				altPtr[2-i] = rgbVal[i];
				}
			while (*nextRGB != ')')
				nextRGB++;
			nextRGB++;
			}
		else if (sscanf(nextRGB, " #%6x", &rgbVal[0]) == 1)							// Hexadecimal
			{
			if (rgbVal < 0)
				return false;
			for (int i = 0; i < 3; i++)
				{
				altPtr[i] = rgbVal[0]&255;
				rgbVal[0] >>= 8;
				}
			nextRGB = strchr(nextRGB, '#') + 7;
			}
		else
			return false;
		altPtr += 4;
		}
	for (int i = 0; i < 16; i++)
		{
		altBGR0[i].blue = (altBGR1[i].blue*2 + 128)/4;
		altBGR0[i].green = (altBGR1[i].green*2 + 128)/4;
		altBGR0[i].red = (altBGR1[i].red*2 + 128)/4;
		}
	return true;
	}


bool setWinInitial(const char *winDef)
	{																				// Format = <max perc>[,x-pos:y-pos]
	if (!*winDef)																	// Nothing set
		return true;
	int testVal1, testVal2, testVal3;
	char testStr[512];
	if (sscanf(winDef, "%d%s", &testVal1, testStr) == 1)							// Only <max perc>
		if (testVal1 > 0 && testVal1 <= 100)										// 1/100% are absolute minimum/maximum
			{
			winPerc = testVal1;
			return true;
			}
	if (sscanf(winDef, "%d,%d:%d%s", &testVal1, &testVal2, &testVal3, testStr) == 3)	// All parameters
		if (testVal1 > 0 && testVal1 <= 100 && testVal2 >= 0 && testVal3 >= 0)		// X-and y-pos only tested for positive values
			{																		// Values too high are checked later and eventually dropped
			winPerc = testVal1;
			initialX = testVal2;
			initialY = testVal3;
			return true;
			}
	return false;
	}

void GUI_StartUp()
	{
	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	GetStartupInfo(&si);
	SDL_WM_SetCaption("vDos", "vDos");
	char *caption = (char*)tempBuff32K;
	strcpy(caption, si.lpTitle);
	if (caption = strrchr(caption, '\\'))
		{
		if (char *p = strrchr(caption, '.'))
			*p = 0;
		SDL_WM_SetCaption(caption+1, caption+1);
		}
	SDL_SysWMinfo wminfo;
	SDL_VERSION(&wminfo.version);
	SDL_GetWMInfo(&wminfo);
	sdlHwnd = wminfo.window;

	SetClassLong(sdlHwnd, GCL_HICON, (LONG)LoadIcon(GetModuleHandle(NULL), "vDos_ico"));	// Set vDos (SDL) icon
	char * fName = ConfGetString("font");
	if (*fName == '-')																// (Preceeding) - indicates ASCII 176-223 = lines
		{
		fontBoxed = true;
		fName = lTrim(fName+1);
		}
	if (*fName)
		readTTF(fName);
	else
		ttf.vDos = true;;

	ttf.lins = ConfGetInt("lins");
	ttf.lins = max(24, min(txtMaxLins, ttf.lins));
	ttf.cols = ConfGetInt("cols");
	ttf.cols = max(80, min(txtMaxCols, ttf.cols));
	for (Bitu i = 0; ModeList_VGA[i].mode <= 7; i++)								// Set the cols and lins in video mode 2,3,7
		{
		ModeList_VGA[i].twidth = ttf.cols;
		ModeList_VGA[i].theight = ttf.lins;
		}

	usesMouse = ConfGetBool("mouse");
//	wpVersion = ConfGetInt("wp");													// If negative, exclude some WP stuff in the future
	winFramed = ConfGetBool("frame");
	sdl.scale = ConfGetInt("scale");
	char *wpStr = ConfGetString("WP");
	if (*wpStr)
		if (!getWP(wpStr))
			ConfAddError("Invalid WP= parameters\n", wpStr);
	rgbColors = altBGR1;
	char * colors = ConfGetString("colors");
	if (!strnicmp(colors, "mono", 4))												// Mono, "Hercules" mode?
		{
		colors += 4;
		lTrim(colors);
		if (*colors == ',' || *colors == 0)
			{
			initialvMode = 7;
			if (*colors == ',')
				{
				colors += 1;
				lTrim(colors);
				}
			}
		}
	if (!setColors(colors))
		{
		setColors("#000000 #0000aa #00aa00 #00aaaa #aa0000 #aa00aa #aa5500 #aaaaaa #555555 #5555ff #55ff55 #55ffff #ff5555 #ff55ff #ffff55 #ffffff");	// Standard DOS colors
		if (*colors)
			{
			initialvMode = 3;
			ConfAddError("Invalid COLORS= parameters\n", colors);
			}
		}
	char * winDef = ConfGetString("window");
	if (!setWinInitial(winDef))
		ConfAddError("Invalid WINDOWS= parameters\n", winDef);
	if (winPerc == 100)
		ttf.fullScrn = true;

	int maxWidth = GetSystemMetrics(SM_CXSCREEN);
	int maxHeight = GetSystemMetrics(SM_CYSCREEN);
	if (!ttf.fullScrn && winFramed)													// 3D borders
		{
		maxWidth -= GetSystemMetrics(SM_CXBORDER)*2;
		maxHeight -= GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYBORDER)*2;
		}

	if (sdl.scale < 1 || sdl.scale > 9)												// 0 = probably not in config.txt, max = 9
		{
		sdl.scale = 1;																// Parachute to 1
		sdl.scale = min(maxWidth/640, maxHeight/480);								// Based on max resolution supported VGA modes
		if (sdl.scale < 1)															// Probably overkill
			sdl.scale = 1;
		if (sdl.scale > 9)
			sdl.scale = 9;
		}

	int	curSize = 30;																// No clear idea what would be a good starting value
	int lastGood = -1;
	int trapLoop = 0;

	while (curSize != lastGood)
		{
		GFX_SelectFontByPoints(curSize);
		if (ttf.cols*ttf.width <= maxWidth && ttf.lins*ttf.height <= maxHeight)		// If it fits on screen
			{
			lastGood = curSize;
			float coveredPerc = float(100*ttf.cols*ttf.width/maxWidth*ttf.lins*ttf.height/maxHeight);
			if (trapLoop++ > 4 && coveredPerc <= winPerc)							// We can get into a +/-/+/-... loop!
				break;
			curSize = (int)(curSize*sqrt((float)winPerc/coveredPerc));				// Rounding down is ok
			if (curSize < 12)														// Minimum size = 12
				curSize = 12;
			}
		else if (--curSize < 12)													// Silly, but OK, youy never know..
			E_Exit("Cannot accommodate a window for %dx%d", ttf.lins, ttf.cols);
		}
	if (ttf.vDos)																	// Make it even for vDos internal font (a bit nicer)
		curSize &= ~1;

	GFX_SelectFontByPoints(curSize);
	HMENU hSysMenu = GetSystemMenu(sdlHwnd, FALSE);

	RemoveMenu(hSysMenu, SC_MAXIMIZE, MF_BYCOMMAND);								// Remove some useless items
	RemoveMenu(hSysMenu, SC_SIZE, MF_BYCOMMAND);
	RemoveMenu(hSysMenu, SC_RESTORE, MF_BYCOMMAND);

	AppendMenu(hSysMenu, MF_SEPARATOR, NULL, "");
    AppendMenu(hSysMenu, MF_STRING, WM_SC_SYSMENUPCOPY,		MSG_Get("SYSMENU:COPY"));
    AppendMenu(hSysMenu, MF_STRING, WM_SC_SYSMENUPASTE,		MSG_Get("SYSMENU:PASTE"));
	AppendMenu(hSysMenu, MF_STRING, WM_SC_SYSMENUDECREASE,	MSG_Get("SYSMENU:DECREASE"));
	AppendMenu(hSysMenu, MF_STRING, WM_SC_SYSMENUINCREASE,	MSG_Get("SYSMENU:INCREASE"));
    AppendMenu(hSysMenu, MF_SEPARATOR, NULL, "");
    AppendMenu(hSysMenu, MF_STRING, WM_SC_SYSMENUABOUT,		MSG_Get("SYSMENU:ABOUT"));

    fnSDLDefaultWndProc = (WNDPROC)GetWindowLongPtr(wminfo.window, GWLP_WNDPROC);
    SetWindowLongPtr(sdlHwnd, GWLP_WNDPROC, (LONG_PTR)&SysMenuExtendWndProc);
	return;
	}


static Bit16u deadKey = 0;
static int winMoving = 0;


void GFX_Events()
	{
	SDL_Event event;
	while (SDL_PollEvent(&event))
		{
		if (!newAttrChar)															// First message is SDL_MOUSEMOTION w/o newAttrChar being initialized
			return;
		if (vga.mode == M_TEXT && !winFramed)
			if (GetFocus() != sdlHwnd)
				{
				if (hasFocus)
					{
					hasFocus = false;
					memset(curAttrChar, -1, ttf.cols*2);							// Force redraw of top line
					GFX_EndTextLines();
					}
				}
			else if (!hasFocus)
				{
				hasFocus = true;
				memset(curAttrChar, -1, ttf.cols*2);								// Force redraw of top line
				GFX_EndTextLines();
				}

		switch (event.type)
			{
		case SDL_MOUSEMOTION:
			if (selectingText)
				{
				RECT selBox1 = {selPosX1*ttf.width, selPosY1*ttf.height, selPosX2*ttf.width+ttf.width, selPosY2*ttf.height+ttf.height};
				int newX = event.button.x/ttf.width;
				selPosX1 = min(selStartX, newX);
				selPosX2 = max(selStartX, newX);
				int newY = event.button.y/ttf.height;
				selPosY1 = min(selStartY, newY);
				selPosY2 = max(selStartY, newY);
				RECT selBox2 = {selPosX1*ttf.width, selPosY1*ttf.height, selPosX2*ttf.width+ttf.width, selPosY2*ttf.height+ttf.height};
				if (memcmp(&selBox1, &selBox2, sizeof(RECT)))						// Update selected block if needed
					{
					HDC hDC = GetDC(sdlHwnd);
					InvertRect(hDC, &selBox1);
					InvertRect(hDC, &selBox2);
					ReleaseDC(sdlHwnd, hDC);
					}
				return;
				}
			if (vga.mode == M_TEXT && !hasFocus)
				break;
			if (vga.mode == M_TEXT && !winMoving && !winFramed)
				if ((!ttf.fullScrn && (event.motion.y < ttf.offY+ttf.height) != hasMinButton))
					{
					hasMinButton = !hasMinButton;
					curAttrChar[ttf.cols-1] = newAttrChar[ttf.cols-1]^0xffff;
					GFX_EndTextLines();
					}
			if (winMoving && ((winMoving++)&1))										// Moving the window generates a second SDL_MOUSEMOTION, just ignore that one
				{
				RECT SDLRect;
				GetWindowRect(sdlHwnd, &SDLRect);
				// Repaint true cause of Windows XP
				MoveWindow(sdlHwnd, SDLRect.left+event.motion.xrel, SDLRect.top+event.motion.yrel, SDLRect.right - SDLRect.left, SDLRect.bottom - SDLRect.top, TRUE);
				break;
				}
			if (vga.mode == M_TEXT && usesMouse)
				{
				if (ttf.fullScrn)
					{
					event.motion.x -= fullLeftOff;
					event.motion.y -= fullTopOff;
					if (event.motion.x < 0 || event.motion.x >= sdl.width || event.motion.y < 0 || event.motion.y >= sdl.height)
						break;
					}
				Mouse_CursorMoved(event.motion.x, event.motion.y, ttf.width, ttf.height);
				}
			else if (vga.mode != M_TEXT)
				Mouse_CursorMoved(event.motion.x, event.motion.y, sdl.scale, sdl.scale);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			{
			if (aboutShown)
				{
				SDL_UpdateRect(sdl.surface, 0, 0, 0, 0);							// Too much and not always needed, so?
				aboutShown = false;
				}
			if (selectingText)														// End selecting text
				{
				SDL_UpdateRect(sdl.surface, 0, 0, 0, 0);							// To restore the window
				SDL_WM_GrabInput(SDL_GRAB_OFF);										// Free mouse
				if (OpenClipboard(NULL))
					{
					if (EmptyClipboard())
						{
						HGLOBAL hCbData = GlobalAlloc(NULL, 2*(selPosY2-selPosY1+1)*(selPosX2-selPosX1+3)-2);
						Bit16u* pChData = (Bit16u*)GlobalLock(hCbData);
						if (pChData)
							{
							int dataIdx = 0;
							for (int line = selPosY1; line <= selPosY2; line++)
								{
								Bit16u *curAC = curAttrChar + line*ttf.cols + selPosX1;
								int lastNotSpace = dataIdx;
								for (int col = selPosX1; col <= selPosX2; col++)
									{
									Bit8u textChar =  *(curAC++)&255;
									pChData[dataIdx++] = cpMap[textChar];
									if (textChar != 32)
										lastNotSpace = dataIdx;
									}
								dataIdx = lastNotSpace;
								if (line != selPosY2)								// Append line feed for all bur the last line 
									{
									pChData[dataIdx++] = 0x0d;
									pChData[dataIdx++] = 0x0a;
									}
								curAC += ttf.cols;
								}
							pChData[dataIdx] = 0;
							SetClipboardData(CF_UNICODETEXT, hCbData);
							GlobalUnlock(hCbData);
							}
						}
					CloseClipboard();
					}

				selectingText = false;
				return;
				}
			// [Win][Ctrl+Left mouse button starts block select for clipboard copy
			if (vga.mode == M_TEXT && !ttf.fullScrn && event.button.state == SDL_PRESSED && ((GetKeyState(VK_LCONTROL)&0x80) || (GetKeyState(VK_RCONTROL)&0x80)) && ((GetKeyState(VK_LWIN)&0x80) || (GetKeyState(VK_RWIN)&0x80)))
				{
				selStartX = selPosX1 = selPosX2 = event.button.x/ttf.width;
				selStartY = selPosY1 = selPosY2 = event.button.y/ttf.height;
				RECT selBox = {selPosX1*ttf.width, selPosY1*ttf.height, selPosX2*ttf.width+ttf.width, selPosY2*ttf.height+ttf.height};
				HDC hDC = GetDC(sdlHwnd);
				InvertRect(hDC, &selBox);
				ReleaseDC(sdlHwnd, hDC);
				SDL_WM_GrabInput(SDL_GRAB_ON);										// Restrict mouse to window
				selectingText = true;
				return;
				}
			winMoving = 0;
			if (!winFramed && !ttf.fullScrn && vga.mode == M_TEXT && event.button.button == 1 && event.button.state == SDL_PRESSED)	//Minimize?
				if (event.button.y < ttf.offY+ttf.height)
					{
					if (event.button.y <= ttf.offY+ttf.height/2 && event.button.x >= ttf.offX+sdl.width-ttf.width)
						{
						ShowWindow(sdlHwnd, SW_MINIMIZE);
						return;
						}
					winMoving = 1;
					}
			if (usesMouse && (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT))
				{
				if (ttf.fullScrn)
					{
					event.button.x -= fullLeftOff;
					event.button.y -= fullTopOff;
					if (event.button.x < 0 || event.button.x >= sdl.width || event.button.y < 0 || event.button.y >= sdl.height)
						break;
					}
				if (event.button.state == SDL_PRESSED)
					Mouse_ButtonPressed(event.button.button>>1);
				else
					Mouse_ButtonReleased(event.button.button>>1);
				}
			break;
			}
		case SDL_QUIT:
			{
			for (Bit8u handle = 0; handle < DOS_FILES; handle++)
				if (Files[handle])
					if ((Files[handle]->GetInformation() & 0x8000) == 0)			// If not device
						{
						MessageBox(sdlHwnd, MSG_Get("UNSAFETOCLOSE2"), MSG_Get("UNSAFETOCLOSE1"), MB_OK | MB_ICONWARNING);
						return;
						}
			SDL_Quit();
			exit(0);
			break;
			}
		case SDL_VIDEOEXPOSE:
			render.cache.nextInvalid = true;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (aboutShown)
				{
				SDL_UpdateRect(sdl.surface, 0, 0, 0, 0);							// Too much and not always needed, so?
				aboutShown = false;
				}
			// [Alt][Enter] siwtches from window to "fullscreen" (should we block these keys and releasing them going to DOS?)
		    if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == 28 && ((event.key.keysym.mod&KMOD_LALT) || (event.key.keysym.mod&KMOD_RALT)))
				{
				if (CurMode->type == M_EGA)
					return;
				ttf.fullScrn = !ttf.fullScrn;
				if (ttf.fullScrn)
					{
					if (GetWindowRect(sdlHwnd, &prevPosition))						// Save position and point size
						prevPointSize = ttf.pointsize;
					int maxWidth = GetSystemMetrics(SM_CXSCREEN);					// Switching to fullscreen should get the maximum sized content
					int maxHeight = GetSystemMetrics(SM_CYSCREEN);
					while (ttf.cols*ttf.width <= maxWidth && ttf.lins*ttf.height <= maxHeight)	// Very lazy method indeed
						GFX_SelectFontByPoints(ttf.pointsize + (ttf.vDos ? 2 : 1));
					GFX_SelectFontByPoints(ttf.pointsize - (ttf.vDos ? 2 : 1));
					ttf.offX = (GetSystemMetrics(SM_CXSCREEN)-ttf.width*ttf.cols)/2;	// Content gets centered
					ttf.offY = (GetSystemMetrics(SM_CYSCREEN)-ttf.height*ttf.lins)/2;
					}
				else
					{
					ttf.offX = ttf.offY = 0;
					if (prevPointSize)												// Switching back to windowed mode
						{
						if (ttf.pointsize != prevPointSize)
							GFX_SelectFontByPoints(prevPointSize);
						}
					else if (winFramed)												// First switch to window mode with frame and maximized font size could give a too big window
						{
						int maxWidth = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXBORDER)*2;
						int maxHeight = GetSystemMetrics(SM_CYSCREEN) - GetSystemMetrics(SM_CYCAPTION) - GetSystemMetrics(SM_CYBORDER)*2;
						if (ttf.cols*ttf.width > maxWidth || ttf.lins*ttf.height > maxHeight)	// If it doesn't fits on screen
							GFX_SelectFontByPoints(ttf.pointsize - (ttf.vDos ? 2 : 1));			// Should do the trick
						}
					}
				GFX_SetSize();
				TimedSetSize();
				if (!ttf.fullScrn && prevPointSize)
					MoveWindow(sdlHwnd, prevPosition.left, prevPosition.top, prevPosition.right-prevPosition.left, prevPosition.bottom-prevPosition.top, false);
				memset(curAttrChar, -1, ttf.cols*ttf.lins*2);
				GFX_EndTextLines();
				return;
				}
			// [Win][Ctrl]+C dumps screen to file screen.txt and opens it with application (default notepad) assigned to .txt files
		    if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == 46 && (event.key.keysym.mod&KMOD_CTRL) && ((GetKeyState(VK_LWIN)&0x80) || (GetKeyState(VK_RWIN)&0x80)))
				{
				if (dumpScreen())
					{
					screendump = true;
					return;
					}
				}
			else if (screendump && event.type == SDL_KEYUP && event.key.keysym.scancode == 46)
				{
				screendump = false;
				return;
				}
			if (event.type == SDL_KEYUP && event.key.keysym.scancode == 0 && event.key.keysym.sym == 306)
				event.key.keysym.scancode = 29;										// Need to repair this after intercepting the 'C' of [Win][Ctr]+C

			// [Win][Ctrl]+V pastes clipboard into keyboard buffer
			if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == 47 && (event.key.keysym.mod&KMOD_CTRL) &&((GetKeyState(VK_LWIN)&0x80) || (GetKeyState(VK_RWIN)&0x80)))
				{
				getClipboard();
				screendump = true;
				return;
				}
			else if (screendump && event.type == SDL_KEYUP && event.key.keysym.scancode == 47)
				{
				screendump = false;
				return;
				}

 			// Pointsize modifiers, we can't use some <Shift>/<Ctrl> combination, SDL_SetVideoMode() messes up SDL keyboard!
			if (event.key.keysym.scancode == 88 && ((GetKeyState(VK_LWIN)&0x80) || (GetKeyState(VK_RWIN)&0x80)))	// Intercept <Win>F12
				{
				if (event.type == SDL_KEYDOWN && !ttf.fullScrn)						// Increase fontsize
					increaseFontSize();
				return;
				}
			if (event.key.keysym.scancode == 87 && ((GetKeyState(VK_LWIN)&0x80) || (GetKeyState(VK_RWIN)&0x80)))	// Intercept <Win>F11
				{
				if (event.type == SDL_KEYDOWN && !ttf.fullScrn)						// Decrease fontsize
					decreaseFontSize();
				return;
				}

			if (event.key.keysym.mod&3)												// Shifted dead keys reported as unshifted
				if (event.key.keysym.sym == 39)										// '=> "
					event.key.keysym.sym = (SDLKey)34;
				else if (event.key.keysym.sym == 96)								// ` => ~
					event.key.keysym.sym = (SDLKey)126;
				else if (event.key.keysym.sym == 54)								// 6 => ^
					event.key.keysym.sym = (SDLKey)94;

			if (!deadKey && event.type == SDL_KEYDOWN && event.key.keysym.unicode == 0)
//			if (!deadKey && event.type == SDL_KEYDOWN)
				if (event.key.keysym.sym == SDLK_QUOTE || event.key.keysym.sym == SDLK_QUOTEDBL || event.key.keysym.sym == SDLK_BACKQUOTE || event.key.keysym.sym == 126 || event.key.keysym.sym == SDLK_CARET)
					{
					deadKey = event.key.keysym.sym;
					return;
					}

			if (deadKey && event.type == SDL_KEYDOWN)
				{
				if (event.key.keysym.sym < 254)
					{
					if (event.key.keysym.unicode > 127)
						for (int i = 128; i < 256; i++)
							if (cpMap[i] == event.key.keysym.unicode)
								{
								BIOS_AddKeyToBuffer(i);
								deadKey = 0;
								return;
								}
					BIOS_AddKeyToBuffer(deadKey);
					if (event.key.keysym.sym != 32)									// If not space, add character
						{
						int correct = 0;
						if (event.key.keysym.sym >= 'a' && event.key.keysym.sym <= 'z')		// Shift reported as unshifted, CAPS not handled
							{
							if (event.key.keysym.mod&KMOD_CAPS)
								correct = -32;
							if (event.key.keysym.mod&(KMOD_LSHIF|KMOD_RSHIFT))
								correct = -(correct+32);
							}
						BIOS_AddKeyToBuffer(event.key.keysym.sym);
						}
					deadKey = 0;
					return;
					}
				}
			if (deadKey && event.type == SDL_KEYUP && event.key.keysym.sym == deadKey)
				return;
			if (event.key.keysym.sym >= SDLK_KP0 && event.key.keysym.sym <= SDLK_KP9 && event.key.keysym.mod&(KMOD_LSHIF|KMOD_RSHIFT) && !(event.key.keysym.mod&SDLK_NUMLOCK))
				event.key.keysym.unicode = event.key.keysym.sym - SDLK_KP0 + 48;	// Why aren't these reported as Unicode?
			BIOS_AddKey(event.key.keysym.scancode, event.key.keysym.unicode, event.key.keysym.sym, event.type == SDL_KEYDOWN);
			break;
			}
		}
	}


static FILE * logMSG;

void GFX_ShowMsg(char const* format, ...)
	{
	if (logMSG != NULL)
		{
		va_list args;
		va_start(args, format);
		vfprintf(logMSG, format, args);
		va_end(args);
		fprintf(logMSG, "\n");
		fflush(logMSG);
		}
	}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
	{
	if (!stricmp(lpCmdLine, "/log"))
		logMSG = fopen("vDos.log", "w");

	try
		{
		MSG_Init();

		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE))						// Init SDL
			E_Exit("Could't init SDL, %s", SDL_GetError());
		if (TTF_Init())																// Init SDL-TTF
			E_Exit("Could't init SDL-TTF, %s", SDL_GetError());

		SDL_putenv("SDL_VIDEO_CENTERED=center");
		SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, 50);
		SDL_EnableUNICODE(true);

		vDOS_Init();
		}
	catch (char * error)
		{
		MessageBox(NULL, error, "vDos - Exception", MB_OK|MB_ICONSTOP);
		}

	SDL_Quit();
	return 0;
	}
