#include "plib/gnw/svga.h"

#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/winmain.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();

// screen rect
Rect scr_size;

// 0x6ACA18
ScreenBlitFunc* scr_blit = GNW95_ShowRect;

SDL_Window* gSdlWindow = NULL;
SDL_Surface* gSdlSurface = NULL;
SDL_Renderer* gSdlRenderer = NULL;
SDL_Texture* gSdlTexture = NULL;
SDL_Surface* gSdlTextureSurface = NULL;

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

// CE: what changed on the texture's surface since the last present: only
// that is uploaded (all of a big screen took over a core 60 times a second).
static SDL_Rect svga_dirty_rect;
static bool svga_dirty = false;
static bool svga_dirty_all = true;

// Testing: the time uploads took (performance counter ticks).
unsigned long long svga_upload_ticks = 0;

static void svga_mark_dirty(const SDL_Rect* rect)
{
    if (rect == NULL) {
        svga_dirty_all = true;
        return;
    }
    if (rect->w <= 0 || rect->h <= 0) {
        return;
    }
    if (!svga_dirty) {
        svga_dirty_rect = *rect;
        svga_dirty = true;
    } else {
        SDL_UnionRect(&svga_dirty_rect, rect, &svga_dirty_rect);
    }
}

// 0x4CB310
void GNW95_SetPaletteEntries(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
        svga_mark_dirty(NULL);
    }
}

// 0x4CB568
void GNW95_SetPalette(unsigned char* palette)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
        svga_mark_dirty(NULL);
    }
}

bool svga_screen_only = false;

// 0x4CB850
void GNW95_ShowRect(unsigned char* src, unsigned int srcPitch, unsigned int a3, unsigned int srcX, unsigned int srcY, unsigned int srcWidth, unsigned int srcHeight, unsigned int destX, unsigned int destY)
{
    buf_to_buf(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    if (!svga_screen_only) {
        SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
        svga_mark_dirty(&srcRect);
    }
}

bool svga_init(VideoOptions* video_options)
{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

    if (video_options->fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    }

    gSdlWindow = SDL_CreateWindow(GNW95_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        video_options->width * video_options->scale,
        video_options->height * video_options->scale,
        windowFlags);
    if (gSdlWindow == NULL) {
        return false;
    }

    if (!createRenderer(video_options->width, video_options->height)) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;

        return false;
    }

    gSdlSurface = SDL_CreateRGBSurface(0,
        video_options->width,
        video_options->height,
        8,
        0,
        0,
        0,
        0);
    if (gSdlSurface == NULL) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    scr_size.ulx = 0;
    scr_size.uly = 0;
    scr_size.lrx = video_options->width - 1;
    scr_size.lry = video_options->height - 1;

    mouse_blit_trans = NULL;
    scr_blit = GNW95_ShowRect;
    mouse_blit = GNW95_ShowRect;

    return true;
}

void svga_exit()
{
    destroyRenderer();

    if (gSdlWindow != NULL) {
        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&scr_size);
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == NULL) {
        return false;
    }

    if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
        return false;
    }

    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture == NULL) {
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, NULL, NULL, NULL) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == NULL) {
        return false;
    }
    svga_mark_dirty(NULL);

    return true;
}

static void destroyRenderer()
{
    if (gSdlTextureSurface != NULL) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = NULL;
    }

    if (gSdlTexture != NULL) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = NULL;
    }

    if (gSdlRenderer != NULL) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = NULL;
    }
}

void handleWindowSizeChanged()
{
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());
}

// CE: Called with each presented frame, e.g. to stream the screen to a co-op
// client. NULL when unused.
void (*svga_present_hook)() = NULL;

void renderPresent()
{
    if (svga_present_hook != NULL) {
        svga_present_hook();
    }

    // CE: only what changed is uploaded; all of it now and then anyway, in
    // case the texture lost what it had.
    static unsigned int lastWhole = 0;
    Uint64 uploadStart = SDL_GetPerformanceCounter();
    unsigned int now = SDL_GetTicks();
    if (svga_dirty_all || now - lastWhole >= 1000) {
        SDL_UpdateTexture(gSdlTexture, NULL, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
        lastWhole = now;
    } else if (svga_dirty) {
        SDL_Rect bounds = { 0, 0, gSdlTextureSurface->w, gSdlTextureSurface->h };
        SDL_Rect rect;
        if (SDL_IntersectRect(&svga_dirty_rect, &bounds, &rect)) {
            const unsigned char* pixels = (const unsigned char*)gSdlTextureSurface->pixels
                + (size_t)rect.y * gSdlTextureSurface->pitch + (size_t)rect.x * gSdlTextureSurface->format->BytesPerPixel;
            SDL_UpdateTexture(gSdlTexture, &rect, pixels, gSdlTextureSurface->pitch);
        }
    }
    svga_dirty = false;
    svga_dirty_all = false;
    svga_upload_ticks += SDL_GetPerformanceCounter() - uploadStart;

    SDL_RenderClear(gSdlRenderer);
    SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout
