/*
 * SFX Player - AmigaBridge Client Demo
 *
 * Simple sound effect player using audio.device to play
 * waveforms on Paula channels. Generates sine, square,
 * sawtooth, and noise waveforms in chip RAM.
 *
 * Demonstrates: audio.device, chip RAM allocation,
 * hooks, variables, memory regions via bridge.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/audio.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Waveform sample size */
#define SAMPLE_LEN 256

/* PAL clock constant for period calculation */
#define PAL_CLOCK 3546895L

/* Channel allocation map - one bit per channel */
static UBYTE channel_map[] = { 1, 2, 4, 8 };

/* Bridge-exposed variables */
static LONG waveform = 0;    /* 0=sine, 1=square, 2=saw, 3=noise */
static LONG frequency = 440;
static LONG volume = 64;     /* 0-64 */
static LONG channel = 0;     /* 0-3 */
static LONG playing = 0;

/* Audio device state */
static struct MsgPort *audio_port = NULL;
static struct IOAudio *audio_io = NULL;
static BOOL audio_open = FALSE;

/* Waveform data in chip RAM */
static BYTE *sample_data = NULL;

/* Sine table - 256 entries of signed byte values */
static const BYTE sine_table[SAMPLE_LEN] = {
      0,   3,   6,   9,  12,  15,  18,  21,  24,  27,  30,  33,  36,  39,  42,  45,
     48,  51,  54,  57,  59,  62,  65,  67,  70,  73,  75,  78,  80,  82,  85,  87,
     89,  91,  94,  96,  98, 100, 102, 103, 105, 107, 108, 110, 112, 113, 114, 116,
    117, 118, 119, 120, 121, 122, 123, 123, 124, 125, 125, 126, 126, 126, 126, 126,
    127, 126, 126, 126, 126, 126, 125, 125, 124, 123, 123, 122, 121, 120, 119, 118,
    117, 116, 114, 113, 112, 110, 108, 107, 105, 103, 102, 100,  98,  96,  94,  91,
     89,  87,  85,  82,  80,  78,  75,  73,  70,  67,  65,  62,  59,  57,  54,  51,
     48,  45,  42,  39,  36,  33,  30,  27,  24,  21,  18,  15,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -15, -18, -21, -24, -27, -30, -33, -36, -39, -42, -45,
    -48, -51, -54, -57, -59, -62, -65, -67, -70, -73, -75, -78, -80, -82, -85, -87,
    -89, -91, -94, -96, -98,-100,-102,-103,-105,-107,-108,-110,-112,-113,-114,-116,
   -117,-118,-119,-120,-121,-122,-123,-123,-124,-125,-125,-126,-126,-126,-126,-126,
   -127,-126,-126,-126,-126,-126,-125,-125,-124,-123,-123,-122,-121,-120,-119,-118,
   -117,-116,-114,-113,-112,-110,-108,-107,-105,-103,-102,-100, -98, -96, -94, -91,
    -89, -87, -85, -82, -80, -78, -75, -73, -70, -67, -65, -62, -59, -57, -54, -51,
    -48, -45, -42, -39, -36, -33, -30, -27, -24, -21, -18, -15, -12,  -9,  -6,  -3
};

/* Simple pseudo-random number generator */
static ULONG rng_state = 12345;

static LONG rng_next(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (LONG)(rng_state >> 16);
}

/* Generate waveform data into chip RAM buffer */
static void generate_waveform(LONG type)
{
    LONG i;

    if (!sample_data) return;

    switch (type) {
    case 0: /* Sine */
        for (i = 0; i < SAMPLE_LEN; i++) {
            sample_data[i] = sine_table[i];
        }
        break;

    case 1: /* Square */
        for (i = 0; i < SAMPLE_LEN; i++) {
            sample_data[i] = (i < SAMPLE_LEN / 2) ? 127 : -127;
        }
        break;

    case 2: /* Sawtooth */
        for (i = 0; i < SAMPLE_LEN; i++) {
            sample_data[i] = (BYTE)(((i * 255) / SAMPLE_LEN) - 128);
        }
        break;

    case 3: /* Noise */
        for (i = 0; i < SAMPLE_LEN; i++) {
            sample_data[i] = (BYTE)((rng_next() % 256) - 128);
        }
        break;

    default:
        /* Default to sine */
        for (i = 0; i < SAMPLE_LEN; i++) {
            sample_data[i] = sine_table[i];
        }
        break;
    }
}

/* Calculate audio period from frequency */
static UWORD freq_to_period(LONG freq)
{
    LONG period;
    if (freq < 20) freq = 20;
    if (freq > 20000) freq = 20000;
    period = PAL_CLOCK / (freq * SAMPLE_LEN);
    if (period < 124) period = 124;   /* hardware minimum */
    if (period > 65535) period = 65535;
    return (UWORD)period;
}

/* Stop current playback */
static void stop_playback(void)
{
    if (playing && audio_open) {
        AbortIO((struct IORequest *)audio_io);
        WaitIO((struct IORequest *)audio_io);
        playing = 0;
    }
}

/* Start playback with current settings.
 * Channel is already allocated at startup — just stop/restart the CMD_WRITE. */
static void start_playback(void)
{
    UWORD period;

    if (!audio_open || !sample_data) return;

    /* Stop any current playback first */
    stop_playback();

    /* Regenerate waveform */
    generate_waveform(waveform);

    /* Clamp values */
    if (volume < 0) volume = 0;
    if (volume > 64) volume = 64;

    period = freq_to_period(frequency);

    /* Set up audio write — channel already allocated */
    audio_io->ioa_Request.io_Command = CMD_WRITE;
    audio_io->ioa_Request.io_Flags = ADIOF_PERVOL;
    audio_io->ioa_Data = (UBYTE *)sample_data;
    audio_io->ioa_Length = SAMPLE_LEN;
    audio_io->ioa_Period = period;
    audio_io->ioa_Volume = (UWORD)volume;
    audio_io->ioa_Cycles = 0; /* loop forever */

    BeginIO((struct IORequest *)audio_io);
    playing = 1;
    /* NOTE: Do NOT call AB_I/ab_log here! This function is called from
     * hooks and from the main loop. Calling ab_log inside a hook triggers
     * send_and_wait which can deadlock or cause reentrancy issues. */
}

/* --- Bridge Hooks --- */

static int hook_play(const char *args, char *resultBuf, int bufSize)
{
    /* Optional arg: frequency */
    if (args && args[0]) {
        LONG f = 0;
        LONG i = 0;
        while (args[i] >= '0' && args[i] <= '9') {
            f = f * 10 + (args[i] - '0');
            i++;
        }
        if (f > 0) {
            frequency = f;
        }
    }

    start_playback();
    sprintf(resultBuf, "Playing waveform %ld at %ld Hz, vol %ld, ch %ld",
            (long)waveform, (long)frequency, (long)volume, (long)channel);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

static int hook_stop(const char *args, char *resultBuf, int bufSize)
{
    (void)args;
    stop_playback();
    strncpy(resultBuf, "Playback stopped", bufSize - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

static int hook_sweep(const char *args, char *resultBuf, int bufSize)
{
    LONG from_freq = 200;
    LONG to_freq = 2000;
    LONG f, step;

    /* Parse "from to" arguments */
    if (args && args[0]) {
        LONG i = 0;
        LONG val = 0;
        /* Parse first number */
        while (args[i] >= '0' && args[i] <= '9') {
            val = val * 10 + (args[i] - '0');
            i++;
        }
        if (val > 0) from_freq = val;
        /* Skip spaces */
        while (args[i] == ' ') i++;
        /* Parse second number */
        val = 0;
        while (args[i] >= '0' && args[i] <= '9') {
            val = val * 10 + (args[i] - '0');
            i++;
        }
        if (val > 0) to_freq = val;
    }

    /* Perform sweep — use direct audio manipulation without AB_I logging
     * to avoid IPC round-trips that slow down the sweep. */
    {
        LONG range = to_freq - from_freq;
        LONG steps = 40; /* Fixed step count to keep within timeout */
        LONG i;
        if (range < 0) range = -range;
        if (steps > range) steps = range;
        if (steps < 1) steps = 1;

        for (i = 0; i <= steps; i++) {
            UWORD period;
            f = from_freq + (to_freq - from_freq) * i / steps;
            if (f < 20) f = 20;
            period = freq_to_period(f);
            /* Update period directly without full start_playback */
            if (audio_open && playing) {
                stop_playback();
            }
            if (audio_open && sample_data) {
                audio_io->ioa_Request.io_Command = CMD_WRITE;
                audio_io->ioa_Request.io_Flags = ADIOF_PERVOL;
                audio_io->ioa_Data = (UBYTE *)sample_data;
                audio_io->ioa_Length = SAMPLE_LEN;
                audio_io->ioa_Period = period;
                audio_io->ioa_Volume = (UWORD)volume;
                audio_io->ioa_Cycles = 0;
                BeginIO((struct IORequest *)audio_io);
                playing = 1;
            }
            Delay(1);
        }
    }

    /* Leave at final frequency */
    frequency = to_freq;

    sprintf(resultBuf, "Sweep %ld -> %ld Hz complete",
            (long)from_freq, (long)to_freq);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

static int hook_noise(const char *args, char *resultBuf, int bufSize)
{
    LONG old_waveform = waveform;
    (void)args;

    /* Switch to noise, play briefly, then restore.
     * Use direct audio control to avoid AB_I IPC delays. */
    waveform = 3;
    generate_waveform(3);
    if (audio_open && sample_data) {
        UWORD period = freq_to_period(frequency);
        if (playing) {
            AbortIO((struct IORequest *)audio_io);
            WaitIO((struct IORequest *)audio_io);
        }
        audio_io->ioa_Request.io_Command = CMD_WRITE;
        audio_io->ioa_Request.io_Flags = ADIOF_PERVOL;
        audio_io->ioa_Data = (UBYTE *)sample_data;
        audio_io->ioa_Length = SAMPLE_LEN;
        audio_io->ioa_Period = period;
        audio_io->ioa_Volume = (UWORD)volume;
        audio_io->ioa_Cycles = 0;
        BeginIO((struct IORequest *)audio_io);
        playing = 1;
        Delay(25); /* ~500ms burst */
        AbortIO((struct IORequest *)audio_io);
        WaitIO((struct IORequest *)audio_io);
        playing = 0;
    }
    waveform = old_waveform;

    strncpy(resultBuf, "Noise burst played", bufSize - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* --- Display --- */

static const char *waveform_names[] = { "Sine", "Square", "Sawtooth", "Noise" };

static void draw_status(struct RastPort *rp, LONG y)
{
    char buf[80];
    LONG len;
    const char *wf_name;

    /* Clear area */
    SetAPen(rp, 0);
    RectFill(rp, 8, y - 8, 290, y + 60);

    SetAPen(rp, 1);

    if (waveform >= 0 && waveform <= 3) {
        wf_name = waveform_names[waveform];
    } else {
        wf_name = "Unknown";
    }

    sprintf(buf, "Waveform: %s (%ld)", wf_name, (long)waveform);
    len = strlen(buf);
    Move(rp, 10, y);
    Text(rp, buf, len);

    sprintf(buf, "Freq: %ld Hz  Vol: %ld/64", (long)frequency, (long)volume);
    len = strlen(buf);
    Move(rp, 10, y + 12);
    Text(rp, buf, len);

    sprintf(buf, "Channel: %ld  Playing: %s",
            (long)channel, playing ? "YES" : "no");
    len = strlen(buf);
    Move(rp, 10, y + 24);
    Text(rp, buf, len);

    if (playing) {
        UWORD period = freq_to_period(frequency);
        sprintf(buf, "Period: %ld", (long)period);
        len = strlen(buf);
        Move(rp, 10, y + 36);
        Text(rp, buf, len);
    }
}

/* Draw a simple visualization of the waveform buffer */
static void draw_waveform(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h)
{
    LONG i, px, py;
    LONG mid = y + h / 2;

    /* Clear area */
    SetAPen(rp, 0);
    RectFill(rp, x, y, x + w, y + h);

    if (!sample_data) return;

    /* Draw center line */
    SetAPen(rp, 2);
    Move(rp, x, mid);
    Draw(rp, x + w, mid);

    /* Draw waveform */
    SetAPen(rp, 3);
    for (i = 0; i < w && i < SAMPLE_LEN; i++) {
        px = x + i;
        /* Scale sample (-128..127) to display height */
        py = mid - (sample_data[(i * SAMPLE_LEN) / w] * (h / 2)) / 128;
        if (py < y) py = y;
        if (py > y + h) py = y + h;

        if (i == 0) {
            Move(rp, px, py);
        } else {
            Draw(rp, px, py);
        }
    }
}

/* --- Main --- */

int main(void)
{
    struct Window *win;
    struct IntuiMessage *msg;
    struct RastPort *rp;
    ULONG class;
    BOOL loop = TRUE;
    LONG hb_counter = 0;
    LONG prev_waveform = -1;
    LONG prev_freq = frequency;
    LONG prev_volume = volume;
    UBYTE alloc_chan;

    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    printf("sfx_player starting\n");

    /* Allocate chip RAM for waveform data (Paula requires it) */
    sample_data = (BYTE *)AllocMem(SAMPLE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (!sample_data) {
        printf("Failed to allocate chip RAM for samples\n");
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Generate initial waveform */
    generate_waveform(waveform);

    /* Open audio.device */
    audio_port = CreateMsgPort();
    if (!audio_port) {
        printf("Failed to create message port\n");
        FreeMem(sample_data, SAMPLE_LEN);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    audio_io = (struct IOAudio *)CreateIORequest(audio_port,
                                                  sizeof(struct IOAudio));
    if (!audio_io) {
        printf("Failed to create IO request\n");
        DeleteMsgPort(audio_port);
        FreeMem(sample_data, SAMPLE_LEN);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Allocate channel 0 initially */
    alloc_chan = channel_map[0];
    audio_io->ioa_Request.io_Message.mn_ReplyPort = audio_port;
    audio_io->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audio_io->ioa_Data = &alloc_chan;
    audio_io->ioa_Length = 1;

    if (OpenDevice((CONST_STRPTR)"audio.device", 0,
                   (struct IORequest *)audio_io, 0) != 0) {
        printf("Failed to open audio.device\n");
        DeleteIORequest((struct IORequest *)audio_io);
        DeleteMsgPort(audio_port);
        FreeMem(sample_data, SAMPLE_LEN);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }
    audio_open = TRUE;
    printf("audio.device opened\n");

    /* Connect to AmigaBridge daemon */
    if (ab_init("sfx_player") != 0) {
        printf("Bridge: NOT FOUND\n");
    } else {
        printf("Bridge: CONNECTED\n");
    }

    AB_I("SFX Player starting");

    /* Register variables */
    ab_register_var("waveform", AB_TYPE_I32, &waveform);
    ab_register_var("frequency", AB_TYPE_I32, &frequency);
    ab_register_var("volume", AB_TYPE_I32, &volume);
    ab_register_var("channel", AB_TYPE_I32, &channel);
    ab_register_var("playing", AB_TYPE_I32, &playing);

    /* Register hooks */
    ab_register_hook("play", "Play sound (optional arg: freq in Hz)", hook_play);
    ab_register_hook("stop", "Stop playback", hook_stop);
    ab_register_hook("sweep", "Frequency sweep (args: from_hz to_hz)", hook_sweep);
    ab_register_hook("noise", "Play random noise burst", hook_noise);

    /* Register memory region for waveform data inspection */
    ab_register_memregion("waveform_data", sample_data, SAMPLE_LEN,
                          "Current 256-byte waveform sample in chip RAM");

    /* Open status window */
    win = OpenWindowTags(NULL,
        WA_Left, 100,
        WA_Top, 50,
        WA_Width, 300,
        WA_Height, 180,
        WA_Title, (ULONG)"SFX Player",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        WA_GimmeZeroZero, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        goto cleanup;
    }

    rp = win->RPort;
    AB_I("Window opened, ready to play");

    /* Main loop */
    while (loop) {
        /* Check window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (class == IDCMP_CLOSEWINDOW) {
                loop = FALSE;
                AB_I("Close requested");
            }
        }
        if (!loop) break;

        /* Check for CTRL-C */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            AB_I("Break signal received");
            loop = FALSE;
            break;
        }

        /* Poll bridge for commands */
        ab_poll();

        /* Restart playback if frequency, volume, or waveform changed while playing */
        if (playing && (frequency != prev_freq || waveform != prev_waveform
                        || volume != prev_volume)) {
            start_playback();
        }

        /* Regenerate waveform display if type changed while not playing */
        if (!playing && waveform != prev_waveform) {
            generate_waveform(waveform);
        }
        prev_waveform = waveform;
        prev_freq = frequency;
        prev_volume = volume;

        /* Update display */
        draw_status(rp, 18);
        draw_waveform(rp, 10, 90, 280, 60);

        /* Heartbeat every ~5 seconds */
        hb_counter++;
        if ((hb_counter % 250) == 0) {
            ab_heartbeat();
            ab_push_var("playing");
            ab_push_var("frequency");
            ab_push_var("waveform");
        }

        /* ~20fps update */
        Delay(3);
    }

    /* Stop playback before exit */
    stop_playback();

    AB_I("SFX Player shutting down");

    CloseWindow(win);

cleanup:
    if (audio_open) {
        CloseDevice((struct IORequest *)audio_io);
    }
    if (audio_io) {
        DeleteIORequest((struct IORequest *)audio_io);
    }
    if (audio_port) {
        DeleteMsgPort(audio_port);
    }

    ab_cleanup();

    if (sample_data) {
        FreeMem(sample_data, SAMPLE_LEN);
    }

    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    printf("sfx_player exited\n");
    return 0;
}
