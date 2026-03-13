#include "sound.h"
#include "tables.h"

#include <exec/memory.h>
#include <devices/audio.h>
#include <proto/exec.h>

/* Big metallic clang - deep body + inharmonic overtones + noise attack */
#define SAMPLE_LENGTH 8000    /* ~0.36 seconds at ~22kHz */
#define SAMPLE_PERIOD  162    /* ~22050 Hz sample rate */
#define SAMPLE_VOLUME   64    /* Max volume */

static struct MsgPort *audio_port = NULL;
static struct IOAudio *audio_io = NULL;
static BYTE *sample_data = NULL;
static UBYTE which_channel[] = { 1, 2, 4, 8 };
static BOOL device_open = FALSE;
static BOOL playing = FALSE;

/* Pseudo-random noise for attack transient */
static WORD noise_state = 12345;
static WORD noise_next(void)
{
    noise_state = (noise_state * 25173 + 13849) & 0x7FFF;
    return (noise_state & 0xFF) - 128;
}

static void synthesize_clank(void)
{
    WORD i;

    if (!sample_data) return;

    for (i = 0; i < SAMPLE_LENGTH; i++) {
        LONG t = ((LONG)i * 256) / SAMPLE_LENGTH;  /* 0..255 */

        /* Envelope: sharp attack, medium decay */
        LONG env = 256 - t;
        if (env < 0) env = 0;
        env = (env * env) >> 8;

        /* Phase accumulators for metallic harmonics (mult * 5.38 ≈ Hz) */
        LONG p0 = ((LONG)i * 19) / 16;    /* ~102 Hz deep body */
        LONG p1 = ((LONG)i * 42) / 16;    /* ~226 Hz resonance */
        LONG p2 = ((LONG)i * 82) / 16;    /* ~441 Hz fundamental */
        LONG p3 = ((LONG)i * 137) / 16;   /* ~737 Hz inharmonic */
        LONG p4 = ((LONG)i * 218) / 16;   /* ~1173 Hz metallic */
        LONG p5 = ((LONG)i * 347) / 16;   /* ~1867 Hz shimmer */

        /* Mix: strong low end for "big" feel */
        LONG val = (sin_table[p0 & 0xFF] * 5) / 4;    /* Deep body (loud) */
        val += sin_table[p1 & 0xFF];                    /* Resonance */
        val += (sin_table[p2 & 0xFF] * 3) / 4;         /* Fundamental */
        val += (sin_table[p3 & 0xFF] * 3) / 4;         /* Inharmonic */
        val += sin_table[p4 & 0xFF] / 2;               /* Metallic */
        val += sin_table[p5 & 0xFF] / 3;               /* Shimmer */

        /* Noise burst for first 300 samples - big clangy attack */
        if (i < 300) {
            LONG noise_amt = ((300 - i) * 3);
            val += (noise_next() * noise_amt) >> 7;
        }

        /* Apply envelope */
        val = (val * env) >> 8;

        /* Hard clip for punchy attack */
        if (val > 127) val = 127;
        if (val < -127) val = -127;

        sample_data[i] = (BYTE)val;
    }
}

BOOL sound_init(void)
{
    audio_port = CreateMsgPort();
    if (!audio_port) return FALSE;

    audio_io = (struct IOAudio *)CreateIORequest(audio_port, sizeof(struct IOAudio));
    if (!audio_io) return FALSE;

    sample_data = (BYTE *)AllocMem(SAMPLE_LENGTH, MEMF_CHIP | MEMF_CLEAR);
    if (!sample_data) return FALSE;

    audio_io->ioa_Request.io_Message.mn_ReplyPort = audio_port;
    audio_io->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audio_io->ioa_Request.io_Command = ADCMD_ALLOCATE;
    audio_io->ioa_Request.io_Flags = ADIOF_NOWAIT;
    audio_io->ioa_AllocKey = 0;
    audio_io->ioa_Data = which_channel;
    audio_io->ioa_Length = sizeof(which_channel);

    if (OpenDevice(AUDIONAME, 0, (struct IORequest *)audio_io, 0) != 0)
        return FALSE;

    device_open = TRUE;
    synthesize_clank();
    return TRUE;
}

void sound_play_boing(void)
{
    if (!device_open || !sample_data) return;

    /* Abort previous sound if still playing */
    if (playing) {
        AbortIO((struct IORequest *)audio_io);
        WaitIO((struct IORequest *)audio_io);
    }

    audio_io->ioa_Request.io_Command = CMD_WRITE;
    audio_io->ioa_Request.io_Flags = ADIOF_PERVOL;
    audio_io->ioa_Data = (UBYTE *)sample_data;
    audio_io->ioa_Length = SAMPLE_LENGTH;
    audio_io->ioa_Period = SAMPLE_PERIOD;
    audio_io->ioa_Volume = SAMPLE_VOLUME;
    audio_io->ioa_Cycles = 1;

    BeginIO((struct IORequest *)audio_io);
    playing = TRUE;
}

void sound_cleanup(void)
{
    if (device_open) {
        if (playing) {
            AbortIO((struct IORequest *)audio_io);
            WaitIO((struct IORequest *)audio_io);
        }
        CloseDevice((struct IORequest *)audio_io);
        device_open = FALSE;
    }

    if (sample_data) {
        FreeMem(sample_data, SAMPLE_LENGTH);
        sample_data = NULL;
    }

    if (audio_io) {
        DeleteIORequest((struct IORequest *)audio_io);
        audio_io = NULL;
    }

    if (audio_port) {
        DeleteMsgPort(audio_port);
        audio_port = NULL;
    }
}
