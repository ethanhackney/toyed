#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

static void enable_raw_mode(void);
static void disable_raw_mode(void);
static void read_keypress(void);
static char read_char(void);
static void refresh(void);
static void clear(void);
static void Write(int fd, const void *buf, size_t count);
static void write_str(int fd, char *str);
static void fatal(int code, char *fmt, ...);
static void draw(void);
static void get_winsize(void);
static void winbuf_init(void);
static void winbuf_puts(char *s);
static void winbuf_free(void);
static void winbuf_flush(void);
static void move(char key);

static struct termios orig_termios;
static int nr_rows;
static int nr_cols;
static char *winbuf;
static size_t winbufsize;
static size_t winbufleng;
static int cursor_row;
static int cursor_col;

int
main(void)
{
        get_winsize();
        enable_raw_mode();

        for (;;) {
                refresh();
                read_keypress();
        }

        return 0;
}

static void
enable_raw_mode(void)
{
        struct termios raw;

        if (tcgetattr(STDIN_FILENO, &orig_termios) < 0)
                err(EX_OSERR, "enable_raw_mode(): tcgetattr()");

        if (atexit(disable_raw_mode) < 0)
                err(EX_OSERR, "enable_raw_mode(): atexit()");

        raw = orig_termios;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
                err(EX_OSERR, "enable_raw_mode(): tcsetattr()");
}

static void
disable_raw_mode(void)
{
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) < 0)
                warn("disable_raw_mode(): tcsetattr()");
}

static void
read_keypress(void)
{
        char c;

        switch ((c = read_char())) {
        case CTRL_KEY('q'):
                clear();
                exit(0);

        case 'h':
        case 'l':
        case 'j':
        case 'k':
                move(c);
                break;
        }
}

static char
read_char(void)
{
        int n;
        char c;

        while ((n = read(STDIN_FILENO, &c, 1)) != 1) {
                if (n < 0 && errno != EAGAIN)
                        fatal(EX_OSERR, "read_char(): read()");
        }

        return c;
}

static void
refresh(void)
{
        char cursor[32];

        winbuf_init();
        winbuf_puts("\x1b[?25l");
        winbuf_puts("\x1b[H");
        draw();

        snprintf(cursor, sizeof(cursor), "\x1b[%d;%dH",
                        cursor_row + 1, cursor_col + 1);
        winbuf_puts(cursor);

        winbuf_puts("\x1b[?25h");
        winbuf_flush();
        winbuf_free();
}

static void
clear(void)
{
        write_str(STDOUT_FILENO, "\x1b[2J");
        write_str(STDOUT_FILENO, "\x1b[H");
}

static void
Write(int fd, const void *buf, size_t count)
{
        ssize_t n;

        if ((n = write(fd, buf, count)) != (ssize_t)count)
                fatal(EX_OSERR, "Write(): write()");
}

static void
fatal(int code, char *fmt, ...)
{
        va_list va;

        clear();

        va_start(va, fmt);
        vfprintf(stderr, fmt, va);
        va_end(va);
        fprintf(stderr, ": %s\r\n", strerror(errno));
        exit(code);
}

static void
draw(void)
{
        int row;

        for (row = 0; row < nr_rows; row++) {
                winbuf_puts("~");
                winbuf_puts("\x1b[K");
                if (row == nr_rows / 2)
                        winbuf_puts("\t\t\tHello world!");
                if (row < nr_rows - 1)
                        winbuf_puts("\r\n");
        }
}

static void
write_str(int fd, char *str)
{
        Write(fd, str, strlen(str));
}

static void
get_winsize(void)
{
        struct winsize ws;

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
                fatal(EX_OSERR, "get_winsize(): ioctl()");

        nr_rows = ws.ws_row;
        nr_cols = ws.ws_col;
}

static void
winbuf_puts(char *s)
{
        size_t len = strlen(s);

        if (winbufleng + len > winbufsize) {
                winbufsize = winbufleng + len;
                winbuf = realloc(winbuf, winbufsize);
                if (!winbuf)
                        fatal(EX_SOFTWARE, "winbuf_puts(): realloc()");
        }

        memcpy(winbuf + winbufleng, s, len);
        winbufleng += len;
}

static void
winbuf_free(void)
{
        free(winbuf);
        winbuf = NULL;
        winbufleng = 0;
        winbufsize = 0;
}

static void
winbuf_init(void)
{
        winbuf = NULL;
        winbufleng = 0;
        winbufsize = 0;
}

static void
winbuf_flush(void)
{
        Write(STDOUT_FILENO, winbuf, winbufleng);
}

static void
move(char key)
{
        switch (key) {
        case 'h':
                if (cursor_col)
                        cursor_col--;
                break;
        case 'l':
                if (cursor_col < nr_cols)
                        cursor_col++;
                break;
        case 'k':
                if (cursor_row)
                        cursor_row--;
                break;
        case 'j':
                if (cursor_row < nr_rows)
                        cursor_row++;
                break;
        }
}
