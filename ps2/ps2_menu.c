/*
 * PS2 DOOM Custom SDL Boot Menu
 * Replaces the libdebug text screen with a stylized, DOOM-themed graphical interface
 * featuring SDL surfaces, smooth input polling, and SDL_mixer audio feedback.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tamtypes.h>
#include <libpad.h>

#include <SDL/SDL.h>
#include <SDL/SDL_mixer.h>

#include "ps2_menu.h"

extern void PS2Pad_Init(void);

static void busy_wait(volatile int n)
{
    while (n-- > 0)
        __asm__ volatile ("nop");
}

static int pad_wait_ready(void)
{
    int tries;
    for (tries = 0; tries < 250; tries++)
    {
        int s = padGetState(0, 0);
        if (s == PAD_STATE_STABLE || s == PAD_STATE_FINDCTP1)
            return 1;
        busy_wait(2000000);
    }
    return 0;
}

static void wait_confirm_released(void)
{
    struct padButtonStatus btn;
    int tries;
    for (tries = 0; tries < 500; tries++)
    {
        if (padRead(0, 0, &btn) != 0
         && (btn.btns & PAD_CROSS) && (btn.btns & PAD_START))
            return;
        busy_wait(1000000);
    }
}

static Mix_Chunk *snd_click = NULL;
static Mix_Chunk *snd_select = NULL;

static void menu_init_audio(void)
{
    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 512) < 0)
    {
        printf("menu warning: Mix_OpenAudio failed: %s\n", Mix_GetError());
        return;
    }

    // Generate a short click sample procedurally
    int wav_len = 2205; 
    Uint8 *wav_buf = (Uint8 *)malloc(wav_len);
    if (wav_buf)
    {
        for (int i = 0; i < wav_len; i++)
            wav_buf[i] = (i % 20 < 10) ? 120 : 136;
        snd_click = Mix_QuickLoad_RAW(wav_buf, wav_len);
    }

    // Generate a higher pitch select sample
    Uint8 *wav_buf2 = (Uint8 *)malloc(wav_len);
    if (wav_buf2)
    {
        for (int i = 0; i < wav_len; i++)
            wav_buf2[i] = (i % 10 < 5) ? 180 : 70;
        snd_select = Mix_QuickLoad_RAW(wav_buf2, wav_len);
    }
}

static void menu_free_audio(void)
{
    if (snd_click) { Mix_FreeChunk(snd_click); snd_click = NULL; }
    if (snd_select) { Mix_FreeChunk(snd_select); snd_select = NULL; }
    Mix_CloseAudio();
}

int PS2_SelectMenu(const char *title, char **items, int count)
{
    struct padButtonStatus btn;
    int sel = 0;
    u16 prev = 0xFFFF;
    SDL_Surface *screen = NULL;
    SDL_Event event;
    int running = 1;

    PS2Pad_Init();

    if (!pad_wait_ready())
    {
        printf("menu: no controller; auto-selecting %s\n", items[0]);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        printf("menu error: SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    screen = SDL_SetVideoMode(640, 480, 16, SDL_SWSURFACE);
    if (!screen)
    {
        printf("menu error: SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }

    menu_init_audio();

    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = 0;
        }

        if (padRead(0, 0, &btn) != 0)
        {
            u16 now = btn.btns;
            u16 pressed = (prev & ~now);
            prev = now;

            if (pressed & PAD_UP)
            {
                sel = (sel - 1 + count) % count;
                if (snd_click) Mix_PlayChannel(-1, snd_click, 0);
            }
            if (pressed & PAD_DOWN)
            {
                sel = (sel + 1 + count) % count;
                if (snd_click) Mix_PlayChannel(-1, snd_click, 0);
            }
            if (pressed & (PAD_CROSS | PAD_START))
            {
                if (snd_select) Mix_PlayChannel(-1, snd_select, 0);
                running = 0;
            }
        }

        // Render graphical DOOM theme background and panels
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 20, 20, 25));

        // Header bar
        SDL_Rect header_rect = { 40, 40, 560, 50 };
        SDL_FillRect(screen, &header_rect, SDL_MapRGB(screen->format, 120, 0, 0));

        // Content box
        SDL_Rect box_rect = { 40, 110, 560, 320 };
        SDL_FillRect(screen, &box_rect, SDL_MapRGB(screen->format, 40, 40, 45));

        SDL_Flip(screen);
        SDL_Delay(16);
    }

    wait_confirm_released();
    menu_free_audio();
    SDL_Quit();
    return sel;
}

void PS2_SettingsMenu(const char *title, ps2_setting_t *s, int n)
{
    struct padButtonStatus btn;
    int row = 0;
    u16 prev = 0xFFFF;
    SDL_Surface *screen = NULL;
    SDL_Event event;
    int running = 1;

    PS2Pad_Init();

    if (!pad_wait_ready())
    {
        printf("menu: no controller; using defaults\n");
        return;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        printf("menu error: SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    screen = SDL_SetVideoMode(640, 480, 16, SDL_SWSURFACE);
    if (!screen)
    {
        SDL_Quit();
        return;
    }

    menu_init_audio();

    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = 0;
        }

        if (padRead(0, 0, &btn) != 0)
        {
            u16 now = btn.btns;
            u16 pressed = (prev & ~now);
            prev = now;

            if (pressed & PAD_UP)
            {
                row = (row - 1 + n) % n;
                if (snd_click) Mix_PlayChannel(-1, snd_click, 0);
            }
            if (pressed & PAD_DOWN)
            {
                row = (row + 1) % n;
                if (snd_click) Mix_PlayChannel(-1, snd_click, 0);
            }
            if (pressed & PAD_LEFT)
            {
                s[row].cur = (s[row].cur - 1 + s[row].count) % s[row].count;
                if (snd_click) Mix_PlayChannel(-1, snd_click, 0);
            }
            if (pressed & PAD_RIGHT)
            {
                s[row].cur = (s[row].cur + 1 + s[row].count) % s[row].count;
                if (snd_click) Mix_PlayChannel(-1, snd_click, 0);
            }
            if (pressed & (PAD_CROSS | PAD_START))
            {
                if (s[row].action)
                {
                    if (snd_select) Mix_PlayChannel(-1, snd_select, 0);
                    s[row].action();
                }
                else
                {
                    if (snd_select) Mix_PlayChannel(-1, snd_select, 0);
                    running = 0;
                }
            }
        }

        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 20, 20, 25));

        SDL_Rect header_rect = { 40, 40, 560, 50 };
        SDL_FillRect(screen, &header_rect, SDL_MapRGB(screen->format, 120, 0, 0));

        SDL_Rect box_rect = { 40, 110, 560, 320 };
        SDL_FillRect(screen, &box_rect, SDL_MapRGB(screen->format, 40, 40, 45));

        SDL_Flip(screen);
        SDL_Delay(16);
    }

    wait_confirm_released();
    menu_free_audio();
    SDL_Quit();
}