#include "Screen.h"

#include <SDL.h>

#include "FileSystemUtils.h"
#include "Game.h"
#include "GraphicsUtil.h"
#include "Vlogging.h"

// Used to create the window icon
extern "C"
{
    extern unsigned lodepng_decode24(
        unsigned char** out,
        unsigned* w,
        unsigned* h,
        const unsigned char* in,
        size_t insize
    );
}

ScreenSettings::ScreenSettings(void)
{
    windowWidth = 320;
    windowHeight = 240;
    fullscreen = false;
    useVsync = true; // Now that uncapped is the default...
    stretch = 0;
    linearFilter = false;
    badSignal = false;
}

void Screen::init(const ScreenSettings& settings)
{
    m_window = NULL;
    m_renderer = NULL;
    m_screenTexture = NULL;
    m_screen = NULL;
    isWindowed = !settings.fullscreen;
    stretchMode = settings.stretch;
    isFiltered = settings.linearFilter;
    vsync = settings.useVsync;
    filterSubrect.x = 1;
    filterSubrect.y = 1;
    filterSubrect.w = 318;
    filterSubrect.h = 238;

    SDL_SetHintWithPriority(
        SDL_HINT_RENDER_SCALE_QUALITY,
        isFiltered ? "linear" : "nearest",
        SDL_HINT_OVERRIDE
    );
    SDL_SetHintWithPriority(
        SDL_HINT_RENDER_VSYNC,
        vsync ? "1" : "0",
        SDL_HINT_OVERRIDE
    );

    // Uncomment this next line when you need to debug -flibit
    // SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);
    // FIXME: m_renderer is also created in resetRendererWorkaround()!
    SDL_CreateWindowAndRenderer(
        640,
        480,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI,
        &m_window,
        &m_renderer
    );
    SDL_SetWindowTitle(m_window, "VVVVVV");

    LoadIcon();

    // FIXME: This surface should be the actual backbuffer! -flibit
    m_screen = SDL_CreateRGBSurface(
        0,
        320,
        240,
        32,
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000
    );
    // ALSO FIXME: This SDL_CreateTexture() is duplicated twice in this file!
    m_screenTexture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        320,
        240
    );

    badSignalEffect = settings.badSignal;

    ResizeScreen(settings.windowWidth, settings.windowHeight);
}

void Screen::destroy(void)
{
#define X(CLEANUP, POINTER) \
    CLEANUP(POINTER); \
    POINTER = NULL;

    /* Order matters! */
    X(SDL_DestroyTexture, m_screenTexture);
    X(SDL_FreeSurface, m_screen);
    X(SDL_DestroyRenderer, m_renderer);
    X(SDL_DestroyWindow, m_window);

#undef X
}

void Screen::GetSettings(ScreenSettings* settings)
{
    int width, height;
    GetWindowSize(&width, &height);

    settings->windowWidth = width;
    settings->windowHeight = height;

    settings->fullscreen = !isWindowed;
    settings->useVsync = vsync;
    settings->stretch = stretchMode;
    settings->linearFilter = isFiltered;
    settings->badSignal = badSignalEffect;
}

void Screen::LoadIcon(void)
{
#ifndef __APPLE__
    unsigned char *fileIn;
    size_t length;
    unsigned char *data;
    unsigned int width, height;
    FILESYSTEM_loadAssetToMemory("VVVVVV.png", &fileIn, &length, false);
    lodepng_decode24(&data, &width, &height, fileIn, length);
    FILESYSTEM_freeMemory(&fileIn);
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom(
        data,
        width,
        height,
        24,
        width * 3,
        SDL_PIXELFORMAT_RGB24
    );
    SDL_SetWindowIcon(m_window, icon);
    SDL_FreeSurface(icon);
    SDL_free(data);
#endif /* __APPLE__ */
}

void Screen::ResizeScreen(int x, int y)
{
    static int resX = 320;
    static int resY = 240;
    if (x != -1 && y != -1)
    {
        // This is a user resize!
        resX = x;
        resY = y;
    }

    if(!isWindowed)
    {
        int result = SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (result != 0)
        {
            vlog_error("Error: could not set the game to fullscreen mode: %s", SDL_GetError());
            return;
        }
    }
    else
    {
        int result = SDL_SetWindowFullscreen(m_window, 0);
        if (result != 0)
        {
            vlog_error("Error: could not set the game to windowed mode: %s", SDL_GetError());
            return;
        }
        if (x != -1 && y != -1)
        {
            SDL_SetWindowSize(m_window, resX, resY);
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    if (stretchMode == 1)
    {
        int winX, winY;
        GetWindowSize(&winX, &winY);
        int result = SDL_RenderSetLogicalSize(m_renderer, winX, winY);
        if (result != 0)
        {
            vlog_error("Error: could not set logical size: %s", SDL_GetError());
            return;
        }
        result = SDL_RenderSetIntegerScale(m_renderer, SDL_FALSE);
        if (result != 0)
        {
            vlog_error("Error: could not set scale: %s", SDL_GetError());
            return;
        }
    }
    else
    {
        SDL_RenderSetLogicalSize(m_renderer, 320, 240);
        int result = SDL_RenderSetIntegerScale(m_renderer, (SDL_bool) (stretchMode == 2));
        if (result != 0)
        {
            vlog_error("Error: could not set scale: %s", SDL_GetError());
            return;
        }
    }
    SDL_ShowWindow(m_window);
}

void Screen::ResizeToNearestMultiple(void)
{
    int w, h;
    GetWindowSize(&w, &h);

    // Check aspect ratio first
    bool using_width;
    int usethisdimension, usethisratio;

    if ((float) w / (float) h > 4.0 / 3.0)
    {
        // Width is bigger, so it's limited by height
        usethisdimension = h;
        usethisratio = 240;
        using_width = false;
    }
    else
    {
        // Height is bigger, so it's limited by width. Or we're exactly 4:3 already
        usethisdimension = w;
        usethisratio = 320;
        using_width = true;
    }

    int floor = (usethisdimension / usethisratio) * usethisratio;
    int ceiling = floor + usethisratio;

    int final_dimension;

    if (usethisdimension - floor < ceiling - usethisdimension)
    {
        // Floor is nearest
        final_dimension = floor;
    }
    else
    {
        // Ceiling is nearest. Or we're exactly on a multiple already
        final_dimension = ceiling;
    }

    if (final_dimension == 0)
    {
        // We're way too small!
        ResizeScreen(320, 240);
        return;
    }

    if (using_width)
    {
        ResizeScreen(final_dimension, final_dimension / 4 * 3);
    }
    else
    {
        ResizeScreen(final_dimension * 4 / 3, final_dimension);
    }
}

void Screen::GetWindowSize(int* x, int* y)
{
    SDL_GetRendererOutputSize(m_renderer, x, y);
}

void Screen::UpdateScreen(SDL_Surface* buffer, SDL_Rect* rect )
{
    if((buffer == NULL) && (m_screen == NULL) )
    {
        return;
    }

    if(badSignalEffect)
    {
        buffer = ApplyFilter(buffer);
    }


    ClearSurface(m_screen);
    BlitSurfaceStandard(buffer,NULL,m_screen,rect);

    if(badSignalEffect)
    {
        SDL_FreeSurface(buffer);
    }

}

const SDL_PixelFormat* Screen::GetFormat(void)
{
    return m_screen->format;
}

void Screen::FlipScreen(const bool flipmode)
{
    SDL_RendererFlip flip_flags;
    if (flipmode)
    {
        flip_flags = SDL_FLIP_VERTICAL;
    }
    else
    {
        flip_flags = SDL_FLIP_NONE;
    }

    SDL_UpdateTexture(
        m_screenTexture,
        NULL,
        m_screen->pixels,
        m_screen->pitch
    );
    SDL_RenderCopyEx(
        m_renderer,
        m_screenTexture,
        isFiltered ? &filterSubrect : NULL,
        NULL,
        0.0,
        NULL,
        flip_flags
    );
    SDL_RenderPresent(m_renderer);
    SDL_RenderClear(m_renderer);
    ClearSurface(m_screen);
}

void Screen::toggleFullScreen(void)
{
    isWindowed = !isWindowed;
    ResizeScreen(-1, -1);

    if (game.currentmenuname == Menu::graphicoptions)
    {
        /* Recreate menu to update "resize to nearest" */
        game.createmenu(game.currentmenuname, true);
    }
}

void Screen::toggleStretchMode(void)
{
    stretchMode = (stretchMode + 1) % 3;
    ResizeScreen(-1, -1);
}

void Screen::toggleLinearFilter(void)
{
    isFiltered = !isFiltered;
    SDL_SetHintWithPriority(
        SDL_HINT_RENDER_SCALE_QUALITY,
        isFiltered ? "linear" : "nearest",
        SDL_HINT_OVERRIDE
    );
    SDL_DestroyTexture(m_screenTexture);
    m_screenTexture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        320,
        240
    );
}

void Screen::toggleVSync(void)
{
#if SDL_VERSION_ATLEAST(2, 0, 17)
    vsync = !vsync;
    SDL_RenderSetVSync(m_renderer, (int) vsync);
#endif
}
