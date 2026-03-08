#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/serial.h>
#include <proto/exec.h>

#include <string.h>

static struct MsgPort *serial_port = NULL;
static struct IOExtSer *serial_io = NULL;
static BOOL device_open = FALSE;

/* Read buffer for incoming data */
static struct IOExtSer *read_io = NULL;

int serial_open(ULONG baud)
{
    if (baud == 0) baud = 9600;

    serial_port = CreateMsgPort();
    if (!serial_port) return -1;

    serial_io = (struct IOExtSer *)CreateIORequest(serial_port,
                                                    sizeof(struct IOExtSer));
    if (!serial_io) {
        DeleteMsgPort(serial_port);
        serial_port = NULL;
        return -1;
    }

    /* Open serial.device unit 0 */
    serial_io->io_SerFlags = SERF_XDISABLED;
    if (OpenDevice((CONST_STRPTR)"serial.device", 0,
                   (struct IORequest *)serial_io, 0) != 0) {
        DeleteIORequest((struct IORequest *)serial_io);
        DeleteMsgPort(serial_port);
        serial_io = NULL;
        serial_port = NULL;
        return -1;
    }
    device_open = TRUE;

    /* Configure: baud rate, 8N1, no handshaking */
    serial_io->IOSer.io_Command = SDCMD_SETPARAMS;
    serial_io->io_Baud = baud;
    serial_io->io_ReadLen = 8;
    serial_io->io_WriteLen = 8;
    serial_io->io_StopBits = 1;
    serial_io->io_SerFlags = SERF_XDISABLED;
    DoIO((struct IORequest *)serial_io);

    /* Create a separate IO request for reading */
    read_io = (struct IOExtSer *)CreateIORequest(serial_port,
                                                  sizeof(struct IOExtSer));
    if (read_io) {
        /* Copy the opened device to the read request */
        CopyMem(serial_io, read_io, sizeof(struct IOExtSer));
    }

    return 0;
}

void serial_close(void)
{
    if (read_io) {
        DeleteIORequest((struct IORequest *)read_io);
        read_io = NULL;
    }

    if (device_open) {
        CloseDevice((struct IORequest *)serial_io);
        device_open = FALSE;
    }

    if (serial_io) {
        DeleteIORequest((struct IORequest *)serial_io);
        serial_io = NULL;
    }

    if (serial_port) {
        DeleteMsgPort(serial_port);
        serial_port = NULL;
    }
}

int serial_write(const char *buf, int len)
{
    if (!device_open || !serial_io) return -1;

    serial_io->IOSer.io_Command = CMD_WRITE;
    serial_io->IOSer.io_Data = (APTR)buf;
    serial_io->IOSer.io_Length = len;
    DoIO((struct IORequest *)serial_io);

    return (int)serial_io->IOSer.io_Actual;
}

int serial_read_available(void)
{
    if (!device_open || !serial_io) return 0;

    serial_io->IOSer.io_Command = SDCMD_QUERY;
    DoIO((struct IORequest *)serial_io);

    return (int)serial_io->IOSer.io_Actual;
}

int serial_read(char *buf, int maxlen)
{
    int avail;

    if (!device_open || !serial_io) return -1;

    avail = serial_read_available();
    if (avail <= 0) return 0;
    if (avail > maxlen) avail = maxlen;

    serial_io->IOSer.io_Command = CMD_READ;
    serial_io->IOSer.io_Data = (APTR)buf;
    serial_io->IOSer.io_Length = avail;
    DoIO((struct IORequest *)serial_io);

    return (int)serial_io->IOSer.io_Actual;
}
