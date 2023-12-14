#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

enum {
        MOVE_LEFT               = 'h',
        MOVE_RIGHT              = 'l',
        MOVE_UP                 = 'k',
        MOVE_DOWN               = 'j',

        MODE_INSERT             = 0,
        MODE_NORMAL             = 1,

        ESCAPE                  = 27,
        ENTER_INSERT_MODE       = 'i',
};

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
static void open_file(char *path);

static struct termios orig_termios;
static int nr_rows;
static int nr_cols;
static char *winbuf;
static size_t winbufsize;
static size_t winbufleng;
static int cursor_row;
static int cursor_col;
static int mode = MODE_NORMAL;
static char *file_row;
static int file_rowsize;
static int nr_lines_in_file;

int
main(int argc, char **argv)
{
        if (argc != 2) {
                errno = EINVAL;
                fatal(EX_USAGE, "usage: a.out path");
        }

        get_winsize();
        enable_raw_mode();

        open_file(argv[1]);
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
        char c = read_char();

        if (mode == MODE_NORMAL) {
                switch (c) {
                case CTRL_KEY('q'):
                        clear();
                        exit(0);

                case MOVE_UP:
                case MOVE_DOWN:
                case MOVE_LEFT:
                case MOVE_RIGHT:
                        move(c);
                        break;

                case 'i':
                        mode = MODE_INSERT;
                        break;
                }
        } else {
                switch (c) {
                case ESCAPE:
                        mode = MODE_NORMAL;
                        break;
                }
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
        size_t len;
        int row;

        for (row = 0; row < nr_rows; row++) {
                if (row >= nr_lines_in_file) {
                        winbuf_puts("~");
                } else {
                        len = file_rowsize;
                        if (len > nr_cols)
                                len = nr_cols;
                        winbuf_puts(file_row);
                }

                winbuf_puts("\x1b[K");
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
        case MOVE_LEFT:
                if (cursor_col)
                        cursor_col--;
                break;
        case MOVE_RIGHT:
                if (cursor_col < nr_cols)
                        cursor_col++;
                break;
        case MOVE_UP:
                if (cursor_row)
                        cursor_row--;
                break;
        case MOVE_DOWN:
                if (cursor_row < nr_rows)
                        cursor_row++;
                break;
        }
}

static void
open_file(char *path)
{
        FILE *fp;
        char *line;
        size_t linecap;
        ssize_t linelen;
        int end;

        if (!(fp = fopen(path, "r")))
                fatal(EX_SOFTWARE, "open_file(): fopen()");

        linecap = 0;
        linelen = getline(&line, &linecap, fp);
        if (linelen != -1) {
                end = line[linelen - 1];
                if (end == '\n' || end == '\r')
                        line[--linelen] = '\0';

                file_rowsize = linelen;
                file_row = malloc(linelen + 1);
                if (!file_row)
                        fatal(EX_SOFTWARE, "open_file(): malloc()");

                memcpy(file_row, line, linelen);
                file_row[linelen] = '\0';
                nr_lines_in_file = 1;
        }
        free(line);

        if (fclose(fp))
                fatal(EX_SOFTWARE, "open_file(): fclose()");
}
