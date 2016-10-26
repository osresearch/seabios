// Video emulation over serial port support
//
// Copyright (C) 2016  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include <stdarg.h> // va_list

#include "biosvar.h" // GET_GLOBAL
#include "output.h" // dprintf
#include "vgabios.h" // struct vgamode_s
#include "vgafb.h" // struct cursorpos
#include "vgautil.h" // sercon_write_pixel
#include "hw/serialio.h" // SEROFF_LSR
#include "x86.h" // inb

#define SERCON_PORT 0x3f8 // XXX - should be configurable
#define SERCON_TIMEOUT 100000


/****************************************************************
 * Serial port writing
 ****************************************************************/

// Write a character to the serial port
static void
sercon_putc(char c)
{
    int timeout = SERCON_TIMEOUT;
    while ((inb(SERCON_PORT+SEROFF_LSR) & 0x20) != 0x20)
        if (!timeout--)
            // Ran out of time.
            return;
    outb(c, SERCON_PORT+SEROFF_DATA);
}

// Write a small unsigned integer in ascii to the serial port
static void
sercon_puti(int v)
{
    switch (v) {
    default:        sercon_putc('0' + ((v / 100) % 10));
    case 10 ... 99: sercon_putc('0' + ((v /  10) % 10));
    case  0 ...  9: sercon_putc('0' + (        v % 10));
    }
}

// Write a multi-byte escape sequence to the serial port
static void
sercon_send_esc(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    sercon_putc('\x1b');
    const char *s = fmt;
    for (;;) {
        char c = GET_GLOBAL(*(u8*)s);
        s++;
        if (!c)
            break;
        if (c == '*') {
            sercon_puti(va_arg(args, int));
            continue;
        }
        sercon_putc(c);
    }
    va_end(args);
}

static void
sercon_set_cursor(struct cursorpos pos)
{
    // XXX - check if already at given position
    sercon_send_esc("[*;*H", pos.y+1, pos.x+1);
}

static void
sercon_set_attr(struct carattr ca)
{
    if (!ca.use_attr)
        // Assume last sent attribute is still valid
        return;
    // XXX - check if new attribute matches last sent attribute
    static VAR16 u8 sercon_cmap[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    u8 fg = 30 + GET_GLOBAL(sercon_cmap[ca.attr & 7]);
    u8 bg = 40 + GET_GLOBAL(sercon_cmap[(ca.attr >> 4) & 7]);
    u8 bold = !!(ca.attr & 0x08);
    if (bold)
        sercon_send_esc("[0;*;*;1m", fg, bg);
    else
        sercon_send_esc("[0;*;*m", fg, bg);
}

static void
sercon_clear_screen(void)
{
    sercon_send_esc("[2J");
}


/****************************************************************
 * Interface functions
 ****************************************************************/

void
sercon_scroll(struct cursorpos win, struct cursorpos winsize
              , int lines, struct carattr ca)
{
    if (winsize.x != GET_BDA(video_cols) || winsize.y != GET_BDA(video_rows)+1)
        // XXX - handle window scrolling?
        return;
    sercon_set_attr(ca);
    if (!lines) {
        sercon_clear_screen();
    } else if (lines > 0) {
        // XXX - send '\n' if lines==1 and on last line
        sercon_send_esc("[*S", lines);
    } else {
        sercon_send_esc("[*T", -lines);
    }
}

void
sercon_write_char(struct cursorpos cp, struct carattr ca)
{
    sercon_set_cursor(cp);
    sercon_set_attr(ca);
    u8 c = ca.car;
    if (c <= 0x1f || c >= 0x7f)
        // XXX - map value to UTF8 code
        c = '?';
    sercon_putc(c);
}

struct carattr
sercon_read_char(struct cursorpos cp)
{
    return (struct carattr){0, 0, 0};
}

void
sercon_write_pixel(u8 color, u16 x, u16 y)
{
}

u8
sercon_read_pixel(u16 x, u16 y)
{
    return 0;
}

#define SERCON_MODE 0x03
static struct vgamode_s sercon_mode VAR16 = {
    MM_TEXT, 80, 25, 4, 9, 16, 0
};

struct vgamode_s *sercon_find_mode(int mode)
{
    if (mode == SERCON_MODE)
        return &sercon_mode;
    return NULL;
}

void
sercon_list_modes(u16 seg, u16 *dest, u16 *last)
{
    if (dest<last) {
        SET_FARVAR(seg, *dest, SERCON_MODE);
        dest++;
    }
    SET_FARVAR(seg, *dest, 0xffff);
}

int
sercon_get_window(struct vgamode_s *vmode_g, int window)
{
    return -1;
}

int
sercon_set_window(struct vgamode_s *vmode_g, int window, int val)
{
    return -1;
}

int
sercon_get_linelength(struct vgamode_s *vmode_g)
{
    return GET_GLOBAL(sercon_mode.width) * 2;
}

int
sercon_set_linelength(struct vgamode_s *vmode_g, int val)
{
    return -1;
}

int
sercon_get_displaystart(struct vgamode_s *vmode_g)
{
    return 0;
}

int
sercon_set_displaystart(struct vgamode_s *vmode_g, int val)
{
    return -1;
}

int
sercon_get_dacformat(struct vgamode_s *vmode_g)
{
    return -1;
}

int
sercon_set_dacformat(struct vgamode_s *vmode_g, int val)
{
    return -1;
}

int
sercon_save_restore(int cmd, u16 seg, void *data)
{
    if (cmd & (SR_HARDWARE|SR_DAC|SR_REGISTERS))
        return -1;
    return bda_save_restore(cmd, seg, data);
}

int
sercon_set_mode(struct vgamode_s *vmode_g, int flags)
{
    if (!(flags & MF_NOCLEARMEM))
        sercon_clear_screen();
    return 0;
}

int
sercon_setup(void)
{
    outb(0x03, SERCON_PORT + SEROFF_LCR); // 8N1
    outb(0x01, SERCON_PORT + 0x02);       // enable fifo
    return 0;
}
