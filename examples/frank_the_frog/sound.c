/*
 * Frank the Frog - Sound effects
 * Uses audio.device with single-shot sample buffers for complex sounds.
 * Each sound effect is a complete waveform played once (cycles=1).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/audio.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>

#include "bridge_client.h"
#include "sound.h"

/* Short waveform for simple repeating tones */
#define WAVE_LEN 32

/* Long single-shot buffers for complex sound effects */
#define SFX_LEN 2048

static struct MsgPort *audio_port = NULL;
static struct IOAudio *audio_io = NULL;

/* Simple waveforms */
static BYTE *wave_blip = NULL;

/* Complex single-shot sound effects */
static BYTE *sfx_jingle = NULL;   /* home arrival jingle */
static BYTE *sfx_splat = NULL;    /* car hit */
static BYTE *sfx_splash = NULL;   /* water splash + glug */
static BYTE *sfx_levelup = NULL;  /* level complete fanfare */
static BYTE *sfx_gameover = NULL; /* game over sad sound */

static int audio_open = 0;
static int io_pending = 0;

static UBYTE alloc_map[] = {1, 2, 4, 8};

/* Simple PRNG */
static ULONG noise_rng = 0x12345678;
static BYTE noise_next(void)
{
    noise_rng ^= noise_rng << 13;
    noise_rng ^= noise_rng >> 17;
    noise_rng ^= noise_rng << 5;
    return (BYTE)(noise_rng & 0xFF);
}

/* Generate a triangle wave sample at position within a cycle of given length */
static BYTE triangle(int pos, int cycle_len, int amplitude)
{
    int quarter = cycle_len / 4;
    int phase = pos % cycle_len;

    if (phase < quarter)
        return (BYTE)(phase * amplitude / quarter);
    else if (phase < 3 * quarter)
        return (BYTE)(amplitude - (phase - quarter) * 2 * amplitude / (2 * quarter));
    else
        return (BYTE)(-amplitude + (phase - 3 * quarter) * amplitude / quarter);
}

/* Generate sine-ish wave (parabolic approximation) */
static BYTE sine_approx(int pos, int cycle_len, int amplitude)
{
    int half = cycle_len / 2;
    int phase = pos % cycle_len;
    int x;

    if (phase < half) {
        x = phase * 256 / half - 128;  /* -128 to 128 */
        return (BYTE)(amplitude - (x * x * amplitude / (128 * 128)));
    } else {
        x = (phase - half) * 256 / half - 128;
        return (BYTE)(-(amplitude - (x * x * amplitude / (128 * 128))));
    }
}

static void build_blip(BYTE *buf)
{
    int i;
    /* Sharp attack, quick decay - good for hop */
    for (i = 0; i < WAVE_LEN / 4; i++)
        buf[i] = 120;
    for (i = WAVE_LEN / 4; i < WAVE_LEN / 2; i++)
        buf[i] = (BYTE)(120 - (i - WAVE_LEN / 4) * 240 / (WAVE_LEN / 4));
    for (i = WAVE_LEN / 2; i < 3 * WAVE_LEN / 4; i++)
        buf[i] = -120;
    for (i = 3 * WAVE_LEN / 4; i < WAVE_LEN; i++)
        buf[i] = (BYTE)(-120 + (i - 3 * WAVE_LEN / 4) * 240 / (WAVE_LEN / 4));
}

static void build_jingle(BYTE *buf)
{
    int i;
    /* Rising 4-note arpeggio: C-E-G-C' encoded as triangle waves */
    /* Note 1: low - cycle length 32 (512 samples = 16 cycles) */
    for (i = 0; i < 512; i++)
        buf[i] = triangle(i, 32, 100);

    /* Note 2: medium - cycle length 25 */
    for (i = 512; i < 1024; i++)
        buf[i] = triangle(i, 25, 110);

    /* Note 3: higher - cycle length 20 */
    for (i = 1024; i < 1536; i++)
        buf[i] = triangle(i, 20, 120);

    /* Note 4: highest - cycle length 16, with decay */
    for (i = 1536; i < SFX_LEN; i++) {
        int amp = 127 - (i - 1536) * 80 / 512;
        if (amp < 20) amp = 20;
        buf[i] = triangle(i, 16, amp);
    }
}

static void build_splat(BYTE *buf)
{
    int i;
    /* Loud noise burst that drops in pitch and volume */
    /* Initial impact: high-amplitude noise */
    for (i = 0; i < 512; i++) {
        int amp = 127 - i * 40 / 512;
        BYTE n = noise_next();
        buf[i] = (BYTE)(n * amp / 127);
    }
    /* Crunch: lower frequency noise (repeat samples for lower pitch effect) */
    for (i = 512; i < 1200; i++) {
        int amp = 87 - (i - 512) * 60 / 688;
        if ((i % 3) == 0) noise_next();  /* slower noise = lower pitch feel */
        buf[i] = (BYTE)(noise_next() * amp / 127);
    }
    /* Fade out */
    for (i = 1200; i < SFX_LEN; i++) {
        int amp = 27 - (i - 1200) * 27 / (SFX_LEN - 1200);
        if (amp < 0) amp = 0;
        buf[i] = (BYTE)(noise_next() * amp / 127);
    }
}

static void build_splash(BYTE *buf)
{
    int i;
    /* Splash: noise burst then bubbly glug-glug-glug */

    /* Initial splash: bright noise */
    for (i = 0; i < 400; i++) {
        int amp = 120 - i * 60 / 400;
        buf[i] = (BYTE)(noise_next() * amp / 127);
    }

    /* Transition: noise fading into tone */
    for (i = 400; i < 600; i++) {
        int noise_amt = 60 - (i - 400) * 50 / 200;
        int tone_amt = (i - 400) * 80 / 200;
        BYTE n = (BYTE)(noise_next() * noise_amt / 127);
        BYTE t = sine_approx(i, 24, tone_amt);
        buf[i] = (BYTE)((n + t) / 2);
    }

    /* Glug 1: low bubbly sine with amplitude wobble */
    for (i = 600; i < 1000; i++) {
        int wobble = 60 + sine_approx(i, 80, 30);
        buf[i] = sine_approx(i, 20, wobble);
    }

    /* Glug 2: slightly higher pitch */
    for (i = 1000; i < 1400; i++) {
        int wobble = 50 + sine_approx(i, 70, 25);
        buf[i] = sine_approx(i, 18, wobble);
    }

    /* Glug 3: fading out */
    for (i = 1400; i < 1800; i++) {
        int amp = 40 - (i - 1400) * 35 / 400;
        if (amp < 5) amp = 5;
        int wobble = amp + sine_approx(i, 60, amp / 2);
        buf[i] = sine_approx(i, 16, wobble);
    }

    /* Silence */
    for (i = 1800; i < SFX_LEN; i++)
        buf[i] = 0;
}

static void build_levelup(BYTE *buf)
{
    int i;
    /* Triumphant ascending fanfare: 5 quick notes going up */

    /* Note 1 */
    for (i = 0; i < 350; i++)
        buf[i] = triangle(i, 28, 110);
    /* Note 2 */
    for (i = 350; i < 700; i++)
        buf[i] = triangle(i, 22, 115);
    /* Note 3 */
    for (i = 700; i < 1050; i++)
        buf[i] = triangle(i, 18, 120);
    /* Note 4 */
    for (i = 1050; i < 1400; i++)
        buf[i] = triangle(i, 14, 125);
    /* Note 5: sustained high with vibrato */
    for (i = 1400; i < SFX_LEN; i++) {
        int vib = 11 + sine_approx(i, 40, 2);
        int amp = 127 - (i - 1400) * 60 / (SFX_LEN - 1400);
        if (amp < 30) amp = 30;
        buf[i] = triangle(i, vib, amp);
    }
}

static void build_gameover(BYTE *buf)
{
    int i;
    /* Sad descending notes */

    /* Note 1: high start */
    for (i = 0; i < 500; i++)
        buf[i] = triangle(i, 16, 100);
    /* Note 2: dropping */
    for (i = 500; i < 1000; i++)
        buf[i] = triangle(i, 22, 90);
    /* Note 3: lower */
    for (i = 1000; i < 1500; i++)
        buf[i] = triangle(i, 30, 80);
    /* Note 4: very low, fading */
    for (i = 1500; i < SFX_LEN; i++) {
        int amp = 70 - (i - 1500) * 60 / (SFX_LEN - 1500);
        if (amp < 10) amp = 10;
        buf[i] = triangle(i, 40, amp);
    }
}

int sound_init(void)
{
    audio_port = CreateMsgPort();
    if (!audio_port) {
        AB_E("sound: CreateMsgPort failed");
        return 1;
    }

    audio_io = (struct IOAudio *)CreateIORequest(audio_port, sizeof(struct IOAudio));
    if (!audio_io) {
        AB_E("sound: CreateIORequest failed");
        DeleteMsgPort(audio_port);
        audio_port = NULL;
        return 1;
    }

    audio_io->ioa_Request.io_Message.mn_Node.ln_Pri = 50;
    audio_io->ioa_Data = alloc_map;
    audio_io->ioa_Length = sizeof(alloc_map);

    if (OpenDevice((CONST_STRPTR)"audio.device", 0,
                   (struct IORequest *)audio_io, 0) != 0) {
        AB_E("sound: OpenDevice failed");
        DeleteIORequest((struct IORequest *)audio_io);
        DeleteMsgPort(audio_port);
        audio_io = NULL;
        audio_port = NULL;
        return 1;
    }

    audio_open = 1;
    io_pending = 0;

    /* Allocate all buffers in chip RAM */
    wave_blip    = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    sfx_jingle   = (BYTE *)AllocMem(SFX_LEN, MEMF_CHIP | MEMF_CLEAR);
    sfx_splat    = (BYTE *)AllocMem(SFX_LEN, MEMF_CHIP | MEMF_CLEAR);
    sfx_splash   = (BYTE *)AllocMem(SFX_LEN, MEMF_CHIP | MEMF_CLEAR);
    sfx_levelup  = (BYTE *)AllocMem(SFX_LEN, MEMF_CHIP | MEMF_CLEAR);
    sfx_gameover = (BYTE *)AllocMem(SFX_LEN, MEMF_CHIP | MEMF_CLEAR);

    if (!wave_blip || !sfx_jingle || !sfx_splat || !sfx_splash ||
        !sfx_levelup || !sfx_gameover) {
        AB_E("sound: chip RAM alloc failed");
        sound_cleanup();
        return 1;
    }

    /* Build all waveforms */
    build_blip(wave_blip);
    build_jingle(sfx_jingle);
    build_splat(sfx_splat);
    build_splash(sfx_splash);
    build_levelup(sfx_levelup);
    build_gameover(sfx_gameover);

    AB_I("sound: init OK, %ld bytes chip RAM", (long)(WAVE_LEN + SFX_LEN * 5));
    return 0;
}

void sound_cleanup(void)
{
    if (audio_open) {
        if (io_pending) {
            AbortIO((struct IORequest *)audio_io);
            WaitIO((struct IORequest *)audio_io);
            io_pending = 0;
        }
        CloseDevice((struct IORequest *)audio_io);
        audio_open = 0;
    }
    if (audio_io) {
        DeleteIORequest((struct IORequest *)audio_io);
        audio_io = NULL;
    }
    if (audio_port) {
        DeleteMsgPort(audio_port);
        audio_port = NULL;
    }
    if (wave_blip)    { FreeMem((APTR)wave_blip,    WAVE_LEN); wave_blip = NULL; }
    if (sfx_jingle)   { FreeMem((APTR)sfx_jingle,   SFX_LEN);  sfx_jingle = NULL; }
    if (sfx_splat)    { FreeMem((APTR)sfx_splat,    SFX_LEN);  sfx_splat = NULL; }
    if (sfx_splash)   { FreeMem((APTR)sfx_splash,   SFX_LEN);  sfx_splash = NULL; }
    if (sfx_levelup)  { FreeMem((APTR)sfx_levelup,  SFX_LEN);  sfx_levelup = NULL; }
    if (sfx_gameover) { FreeMem((APTR)sfx_gameover, SFX_LEN);  sfx_gameover = NULL; }
}

/* Play a short repeating waveform */
static void play_tone(BYTE *wave, UWORD len, UWORD period, UWORD volume, UWORD cycles)
{
    if (!audio_open || !wave) return;

    if (io_pending) {
        AbortIO((struct IORequest *)audio_io);
        WaitIO((struct IORequest *)audio_io);
        while (GetMsg(audio_port)) ;
        io_pending = 0;
    }

    audio_io->ioa_Request.io_Command = CMD_WRITE;
    audio_io->ioa_Request.io_Flags = ADIOF_PERVOL;
    audio_io->ioa_Data = (UBYTE *)wave;
    audio_io->ioa_Length = len;
    audio_io->ioa_Period = period;
    audio_io->ioa_Volume = volume;
    audio_io->ioa_Cycles = cycles;

    BeginIO((struct IORequest *)audio_io);
    io_pending = 1;
}

void sound_hop(void)
{
    /* Quick chirpy blip */
    play_tone(wave_blip, WAVE_LEN, 200, 50, 3);
}

void sound_splat(void)
{
    /* Full splat sound effect - single shot */
    play_tone(sfx_splat, SFX_LEN, 140, 64, 1);
}

void sound_splash(void)
{
    /* Splash + glug-glug - single shot */
    play_tone(sfx_splash, SFX_LEN, 160, 60, 1);
}

void sound_home(void)
{
    /* Rising jingle - single shot */
    play_tone(sfx_jingle, SFX_LEN, 180, 64, 1);
}

void sound_levelup(void)
{
    /* Triumphant fanfare - single shot */
    play_tone(sfx_levelup, SFX_LEN, 160, 64, 1);
}

void sound_gameover(void)
{
    /* Sad descending - single shot */
    play_tone(sfx_gameover, SFX_LEN, 200, 60, 1);
}
