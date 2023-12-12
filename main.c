#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

static void enable_raw_mode(void);
static void disable_raw_mode(void);

static struct termios orig_termios;

int
main(void)
{
        enable_raw_mode();

        for (;;) {
                char c = '\0';

                if (read(STDIN_FILENO, &c, 1) < 0 && errno != EAGAIN)
                        err(EX_OSERR, "main(): read()");

                if (iscntrl(c))
                        printf("%d\r\n", c);
                else
                        printf("%d ('%c')\r\n", c, c);

                if (c == 'q')
                        break;
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
