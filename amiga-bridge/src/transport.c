/*
 * transport.c - Transport dispatch for AmigaBridge daemon.
 *
 * Routes the daemon's I/O to either the serial backend (serial_io.c) or the
 * TCP/bsdsocket backend (net_io.c), chosen at startup via g_transport_mode.
 */
#include <exec/types.h>

#include "bridge_internal.h"

int g_transport_mode = TRANSPORT_SERIAL;

int transport_open(int mode, ULONG param)
{
    g_transport_mode = mode;
    if (mode == TRANSPORT_TCP)
        return net_open(param);
    return serial_open(param);
}

void transport_close(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        net_close();
    else
        serial_close();
}

int transport_write(const char *buf, int len)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_write(buf, len);
    return serial_write(buf, len);
}

void transport_start_read(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return;                 /* no-op: TCP socket is always drainable */
    serial_start_read();
}

int transport_check_read(char *out_byte)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_check_read(out_byte);
    return serial_check_read(out_byte);
}

ULONG transport_get_signal(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_get_signal();
    return serial_get_signal();
}

BOOL transport_is_open(void)
{
    if (g_transport_mode == TRANSPORT_TCP)
        return net_is_open();
    return serial_is_open();
}
