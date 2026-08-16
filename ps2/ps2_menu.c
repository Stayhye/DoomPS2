#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tamtypes.h>
#include <libpad.h>

#include <SDL/SDL.h>

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
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        return 0;
    }

    screen = SDL_SetVideoMode(640, 480, 16, SDL_SWSURFACE);
    if (!screen)
    {
        SDL_Quit();
        return 0;
    }

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
            }
            if (pressed & PAD_DOWN)
            {
                sel = (sel + 1 + count) % count;
            }
            if (pressed & (PAD_CROSS | PAD_START))
            {
                running = 0;
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
    SDL_Quit();
    return sel;
}