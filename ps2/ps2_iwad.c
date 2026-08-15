// PS2 IWAD + PWAD selection.
//
// Scans cdfs (disc/ISO) for IWADs, PWADs, and DeHackEd patches, shows a
// controller setup menu (IWAD / PWAD / DeHackEd / Music / Render), and returns the chosen
// IWAD. The chosen PWAD (if any) is merged by d_main.c right after the IWAD
// (see PS2_GetPWAD + the __PS2__ hook there).
//
// Called from the one PS2 hook in the upstream d_main.c (guarded by __PS2__).

#include <stdio.h>
#include <stdlib.h>     // atoi
#include <string.h>     // strncmp
#include <kernel.h>     // LoadExecPS2
#include <loadfile.h>   // SifExecModuleBuffer
#include <libpwroff.h>  // poweroffInit / poweroffShutdown

#include "ps2_menu.h"   // PS2_SettingsMenu, ps2_setting_t
#include "m_argv.h"     // M_CheckParmWithArgs, myargv

// Power the PS2 down from the setup menu's "Shutdown" row. Loads the poweroff
// IOP module, then asks it to put the console into standby. Does not return on
// real hardware (on PCSX2 it just halts here -- emulators don't power off).
static void PS2_Shutdown(void)
{
    extern unsigned char poweroff_irx[];
    extern unsigned int  size_poweroff_irx;
    int ret;

    printf("PS2OOM: shutting down ...\n");
    SifExecModuleBuffer(poweroff_irx, size_poweroff_irx, 0, NULL, &ret);
    poweroffInit();
    poweroffShutdown();
    for (;;) { }
}

// This ELF's video renderer (set at build time). The three backends live in
// three ELFs on the disc and share one setup screen; choosing a different one
// LoadExec's its ELF with the chosen settings. 0 = SDL2 (software), 1 = gsKit
// (software), 2 = GL (hardware geometry).
#if defined(RENDERER_GL)
#define THIS_RENDERER 2
#elif defined(RENDERER_GS)
#define THIS_RENDERER 1
#else
#define THIS_RENDERER 0
#endif

// Per-renderer re-exec ELFs.
static const char *g_renderer_elf[3] = {
    "cdrom0:\\DOOMSDL.ELF;1",
    "cdrom0:\\DOOMGS.ELF;1",
    "cdrom0:\\DOOMGL.ELF;1",
};

// Music engine chosen at the startup menu: 0 = OPL/FM (audsrv), 1 = SPU2 synth.
// Default OPL (the classic sound); the menu's second option is SPU2. Read by
// i_sound.c's InitMusicModule.
static int g_music_engine = 0;
int PS2_MusicEngine(void) { return g_music_engine; }

// GS output mode chosen at the startup menu: 0 = interlaced (480i/NTSC, any TV),
// 1 = progressive 480p (sharp, no flicker; needs component/YPbPr on real
// hardware, works directly in PCSX2). Honored by the gsKit backend at GS init
// (doomgeneric_ps2_gs.c). Default follows the build flag so a GS480P build still
// defaults to 480p.
#ifdef GS_OUTPUT_480P
static int g_video_mode = 1;
#else
static int g_video_mode = 0;
#endif
int PS2_VideoMode(void) { return g_video_mode; }

// Jump enabled (0 = vanilla, no jumping). Toggled on the setup menu; read by
// p_user.c (P_MovePlayer). Carried across a renderer switch with -jump.
static int g_jump = 0;
int PS2_JumpEnabled(void) { return g_jump; }

// PWAD chosen on the setup menu (or passed via -pwad on a renderer switch).
// NULL = none. d_main.c calls PS2_GetPWAD() after loading the IWAD.
static char *g_pwad_path = NULL;
char *PS2_GetPWAD(void) { return g_pwad_path; }

// DeHackEd patch chosen on the setup menu (or passed via -deh on a renderer switch).
// NULL = none.
static char *g_deh_path = NULL;
char *PS2_GetDehPath(void) { return g_deh_path; }

// Re-exec another ELF.
static void PS2_Exec(const char *path, int argc, char **argv)
{
    LoadExecPS2(path, argc, argv);
}

// Called from I_Quit (DOOM "quit to DOS").
void PS2_ReturnToLauncher(void)
{
#ifdef BOOT_STRAIGHT
    LoadExecPS2("rom0:OSDSYS", 0, NULL);   // -> PS2 system menu; noreturn
#else
    char *args[1];
    args[0] = (char *)g_renderer_elf[THIS_RENDERER];
    PS2_Exec(g_renderer_elf[THIS_RENDERER], 1, args);   // noreturn
#endif
}

// Candidate IWADs to probe on cdfs (disc/ISO).
static char *cd_iwads[] = {
    "cdfs:/IWADS/DOOM.WAD", "cdfs:/IWADS/DOOM2.WAD", "cdfs:/IWADS/PLUTONIA.WAD", 
	"cdfs:/IWADS/TNT.WAD", "cdfs:/IWADS/HERETIC.WAD", "cdfs:/IWADS/HEXEN.WAD", "cdfs:/IWADS/FALLOUT.WAD", NULL
};

// Candidate PWADs on cdfs.
static char *cd_pwads[] = {
    "cdfs:/PWADS/BETRAY.WAD", "cdfs:/PWADS/SEWERS.WAD", "cdfs:/PWADS/SIGIL.WAD",
    "cdfs:/PWADS/TNT31.WAD", "cdfs:/PWADS/SCYTHE.WAD", "cdfs:/PWADS/THATCHER.WAD",
    "cdfs:/PWADS/NUTS.WAD", "cdfs:/PWADS/NUTS2.WAD", "cdfs:/PWADS/NUTS3.WAD", NULL
};

// Candidate DeHackEd patches on cdfs.
static char *cd_deh[] = {
    "cdfs:/IWADS/DOOM.DEH", "cdfs:/IWADS/DOOM2.DEH", "cdfs:/IWADS/PLUTONIA.DEH",
    "cdfs:/IWADS/TNT.DEH", "cdfs:/IWADS/REVERIE.DEH", "cdfs:/DEH/DOOM.DEH", 
    "cdfs:/DEH/DOOM2.DEH", "cdfs:/DEH/PLUTONIA.DEH", "cdfs:/DEH/TNT.DEH", NULL
};

char *PS2_GetIWAD(void)
{
#ifdef EMBED_WAD
    static char embedded_iwad[] = "doom1.wad";   // served from the baked-in array
#endif
    extern void PS2Cdfs_Init(void);
    extern int  PS2Cdfs_Exists(const char *path);

    char *labels[24];     // IWADs shown in the menu
    char *paths[24];      // IWAD path returned to the WAD loader
    int   n = 0;
    char *pw_labels[24];  // PWADs ("None" + whatever is present)
    char *pw_paths[24];   // matching PWAD paths (pw_paths[0] = NULL = none)
    int   pwn = 0;
    char *deh_labels[24]; // DeHackEd patches ("None" + whatever is present)
    char *deh_paths[24];  // matching DeHackEd paths (deh_paths[0] = NULL = none)
    int   dehn = 0;
    int   i;

#ifdef SPU_BEEP
    {
        extern void PS2Spu_BeepTest(void);
        printf("\n=== SPU2 synth S1: hardware voice self-test ===\n");
        PS2Spu_BeepTest();
        printf("spu: halted -- you should hear a steady tone.\n");
        for (;;) { }
    }
#endif

#if defined(BOOT_STRAIGHT) && defined(EMBED_WAD)
    printf("BOOT_STRAIGHT: skipping setup menu, using embedded DOOM1.WAD\n");
    return embedded_iwad;
#endif

    // Launched by the OTHER renderer's ELF (a renderer switch) with explicit settings?
    {
        int pi = M_CheckParmWithArgs("-iwad", 1);
        if (pi > 0)
        {
            int pm = M_CheckParmWithArgs("-music", 1);
            int pp = M_CheckParmWithArgs("-pwad", 1);
            int pd = M_CheckParmWithArgs("-deh", 1);
            int pv = M_CheckParmWithArgs("-video", 1);
            int pj = M_CheckParmWithArgs("-jump", 1);
            if (pm > 0) g_music_engine = atoi(myargv[pm + 1]);
            if (pp > 0) g_pwad_path = myargv[pp + 1];
            if (pd > 0) g_deh_path = myargv[pd + 1];
            if (pv > 0) g_video_mode = atoi(myargv[pv + 1]);
            if (pj > 0) g_jump = atoi(myargv[pj + 1]);
            printf("IWAD (from launcher): %s   pwad: %s   deh: %s   music: %d   video: %d   jump: %d\n",
                   myargv[pi + 1], g_pwad_path ? g_pwad_path : "(none)", g_deh_path ? g_deh_path : "(none)", g_music_engine, g_video_mode, g_jump);
            return myargv[pi + 1];
        }
    }

    // Scan the disc (cdfs).
    printf("IWAD: scanning disc (cdfs)...\n");
    PS2Cdfs_Init();
    for (i = 0; cd_iwads[i] != NULL && n < 24; ++i)
    {
        if (PS2Cdfs_Exists(cd_iwads[i]))
        {
            printf("  %-24s [found]\n", cd_iwads[i]);
            labels[n] = cd_iwads[i];
            paths[n]  = cd_iwads[i];
            n++;
        }
    }

#ifdef EMBED_WAD
    if (n < 24)
    {
        labels[n] = "Embedded shareware DOOM1.WAD";
        paths[n]  = embedded_iwad;
        n++;
    }
#endif

    // Scan for PWADs on cdfs. Index 0 is always "None".
    pw_labels[0] = "None"; pw_paths[0] = NULL; pwn = 1;
    printf("PWAD: scanning...\n");
    for (i = 0; cd_pwads[i] != NULL && pwn < 24; ++i)
        if (PS2Cdfs_Exists(cd_pwads[i]))
        {
            printf("  %-24s [found]\n", cd_pwads[i]);
            pw_labels[pwn] = cd_pwads[i]; pw_paths[pwn] = cd_pwads[i]; pwn++;
        }

    // Scan for DeHackEd patches on cdfs. Index 0 is always "None".
    deh_labels[0] = "None"; deh_paths[0] = NULL; dehn = 1;
    printf("DeHackEd: scanning...\n");
    for (i = 0; cd_deh[i] != NULL && dehn < 24; ++i)
        if (PS2Cdfs_Exists(cd_deh[i]))
        {
            printf("  %-24s [found]\n", cd_deh[i]);
            deh_labels[dehn] = cd_deh[i]; deh_paths[dehn] = cd_deh[i]; dehn++;
        }

    {
        static char  *eng[]  = { "OPL / FM (AdLib)", "SPU2 hardware synth" };
        static char  *rend[] = { "SDL2 (software)", "gsKit (software)", "GL (hardware)" };
        static char  *vid[]  = {
            "NTSC 480i", "NTSC 480p", "PAL 576i", "PAL 576p",
            "720p (exp)", "1080i (exp)"
        };
        static char  *jmp[]  = { "Off (vanilla)", "On (advanced)" };
        static char  *shut[] = { "Power off PS2" };
        ps2_setting_t settings[8] = {0};
        char         *wad;
        int           default_iwad_idx = 0;

        if (n == 0)
        {
            printf("\n  *** No IWAD found ***\n");
            printf("  Supply a WAD on a cdfs disc, or build EMBED_WAD=1.\n");
            for (;;) { }
        }

        // Automatically default selection to match the target game build if available
        for (i = 0; i < n; ++i)
        {
#ifdef HERETIC
            if (strstr(paths[i], "HERETIC.WAD") != NULL) { default_iwad_idx = i; break; }
#elif defined(HEXEN)
            if (strstr(paths[i], "HEXEN.WAD") != NULL) { default_iwad_idx = i; break; }
#elif defined(STRIFE)
            if (strstr(paths[i], "STRIFE") != NULL) { default_iwad_idx = i; break; }
#else
            if (strstr(paths[i], "DOOM.WAD") != NULL || strstr(paths[i], "DOOM2.WAD") != NULL) { default_iwad_idx = i; break; }
#endif
        }

        settings[0].label = "IWAD";     settings[0].values = labels;     settings[0].count = n;    settings[0].cur = default_iwad_idx;
        settings[1].label = "PWAD";     settings[1].values = pw_labels;  settings[1].count = pwn;  settings[1].cur = 0;
        settings[2].label = "DeHackEd"; settings[2].values = deh_labels; settings[2].count = dehn; settings[2].cur = 0;
        settings[3].label = "Music";    settings[3].values = eng;        settings[3].count = 2;    settings[3].cur = g_music_engine;
        settings[4].label = "Render";   settings[4].values = rend;       settings[4].count = 3;    settings[4].cur = 1;
        settings[5].label = "Video";    settings[5].values = vid;        settings[5].count = 6;    settings[5].cur = g_video_mode;
        settings[6].label = "Jump";     settings[6].values = jmp;        settings[6].count = 2;    settings[6].cur = g_jump;
        settings[7].label = "Shutdown"; settings[7].values = shut;     settings[7].count = 1;    settings[7].cur = 0;
        settings[7].action = PS2_Shutdown;

        PS2_SettingsMenu("PS2OOM  --  setup", settings, 8);

        wad            = paths[settings[0].cur];
        g_pwad_path    = pw_paths[settings[1].cur];
        g_deh_path     = deh_paths[settings[2].cur];
        g_music_engine = settings[3].cur;
        g_video_mode   = settings[5].cur;
        g_jump         = settings[6].cur;

        if (settings[4].cur != THIS_RENDERER)
        {
            static char musbuf[4], vidbuf[4], jmpbuf[4];
            char *args[13];
            int   na;
            musbuf[0] = (char)('0' + (g_music_engine & 1)); musbuf[1] = '\0';
            vidbuf[0] = (char)('0' + g_video_mode);         vidbuf[1] = '\0';
            jmpbuf[0] = (char)('0' + (g_jump & 1));         jmpbuf[1] = '\0';
            args[0] = (char *)g_renderer_elf[settings[4].cur];
            args[1] = "-iwad"; args[2] = wad;
            args[3] = "-music"; args[4] = musbuf;
            args[5] = "-video"; args[6] = vidbuf;
            args[7] = "-jump";  args[8] = jmpbuf;
            na = 9;
            if (g_pwad_path) { args[na++] = "-pwad"; args[na++] = g_pwad_path; }
            if (g_deh_path)  { args[na++] = "-deh";  args[na++] = g_deh_path; }
            printf("renderer switch -> %s\n", g_renderer_elf[settings[4].cur]);
            PS2_Exec(g_renderer_elf[settings[4].cur], na, args);
        }

        printf("IWAD: %s   pwad: %s   deh: %s   music: %s   video: %s\n",
               wad, g_pwad_path ? g_pwad_path : "(none)", g_deh_path ? g_deh_path : "(none)", eng[g_music_engine], vid[g_video_mode]);
        return wad;
    }
}