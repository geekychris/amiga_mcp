/*
 * serial_io.c - Async serial device I/O for AmigaBridge daemon
 *
 * Uses SendIO for async CMD_READ with signal-based notification.
 * Write is synchronous (DoIO).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/serial.h>
#include <proto/exec.h>

#include <string.h>

#include "bridge_internal.h"

static struct MsgPort *ser_port = NULL;
static struct IOExtSer *write_io = NULL;
static struct IOExtSer *read_io = NULL;
static BOOL dev_open = FALSE;
static BOOL read_pending = FALSE;
static char read_byte;

int serial_open(ULONG baud)
{
    if (baud == 0) baud = 9600;

    /* Create message port for serial device */
    ser_port = CreateMsgPort();
    if (!ser_port) return -1;

    /* Create write IO request */
    write_io = (struct IOExtSer *)CreateIORequest(ser_port,
                                                   sizeof(struct IOExtSer));
    if (!write_io) {
        DeleteMsgPort(ser_port);
        ser_port = NULL;
        return -1;
    }

    /* Open serial.device unit 0 */
    write_io->io_SerFlags = SERF_XDISABLED;
    if (OpenDevice((CONST_STRPTR)"serial.device", 0,
                   (struct IORequest *)write_io, 0) != 0) {
        DeleteIORequest((struct IORequest *)write_io);
        DeleteMsgPort(ser_port);
        write_io = NULL;
        ser_port = NULL;
        return -1;
    }
    dev_open = TRUE;

    /* Configure: baud rate, 8N1, no handshaking */
    write_io->IOSer.io_Command = SDCMD_SETPARAMS;
    write_io->io_Baud = baud;
    write_io->io_ReadLen = 8;
    write_io->io_WriteLen = 8;
    write_io->io_StopBits = 1;
    write_io->io_SerFlags = SERF_XDISABLED;
    DoIO((struct IORequest *)write_io);

    /* Create separate IO request for async reads */
    read_io = (struct IOExtSer *)CreateIORequest(ser_port,
                                                  sizeof(struct IOExtSer));
    if (!read_io) {
        CloseDevice((struct IORequest *)write_io);
        DeleteIORequest((struct IORequest *)write_io);
        DeleteMsgPort(ser_port);
        write_io = NULL;
        ser_port = NULL;
        dev_open = FALSE;
        return -1;
    }

    /* Copy opened device info to read request */
    CopyMem(write_io, read_io, sizeof(struct IOExtSer));

    read_pending = FALSE;
    return 0;
}

void serial_close(void)
{
    /* Abort pending async read */
    if (read_pending && read_io) {
        AbortIO((struct IORequest *)read_io);
        WaitIO((struct IORequest *)read_io);
        read_pending = FALSE;
    }

    if (read_io) {
        DeleteIORequest((struct IORequest *)read_io);
        read_io = NULL;
    }

    if (dev_open) {
        CloseDevice((struct IORequest *)write_io);
        dev_open = FALSE;
    }

    if (write_io) {
        DeleteIORequest((struct IORequest *)write_io);
        write_io = NULL;
    }

    if (ser_port) {
        DeleteMsgPort(ser_port);
        ser_port = NULL;
    }
}

int serial_write(const char *buf, int len)
{
    int total = 0;

    if (!dev_open || !write_io) return -1;

    /* Write the entire message in one DoIO call.
     * Keep it simple — the chunked approach with SDCMD_QUERY pacing
     * was causing excessive blocking that prevented incoming serial
     * data from being processed. Single-shot write is faster and
     * doesn't starve the read side. */
    write_io->IOSer.io_Command = CMD_WRITE;
    write_io->IOSer.io_Data = (APTR)buf;
    write_io->IOSer.io_Length = len;
    DoIO((struct IORequest *)write_io);
    total = (int)write_io->IOSer.io_Actual;

    return total;
}

void serial_start_read(void)
{
    if (!dev_open || !read_io || read_pending) return;

    read_io->IOSer.io_Command = CMD_READ;
    read_io->IOSer.io_Data = (APTR)&read_byte;
    read_io->IOSer.io_Length = 1;
    SendIO((struct IORequest *)read_io);
    read_pending = TRUE;
}

int serial_check_read(char *out_byte)
{
    struct IORequest *req;

    if (!read_pending || !read_io) return 0;

    req = CheckIO((struct IORequest *)read_io);
    if (!req) return 0;

    /* IO completed, remove from port */
    WaitIO((struct IORequest *)read_io);
    read_pending = FALSE;

    if (read_io->IOSer.io_Error == 0 && read_io->IOSer.io_Actual == 1) {
        *out_byte = read_byte;
        return 1;
    }

    return 0;
}

ULONG serial_get_signal(void)
{
    if (!ser_port) return 0;
    return 1UL << ser_port->mp_SigBit;
}

BOOL serial_is_open(void)
{
    return dev_open;
}
