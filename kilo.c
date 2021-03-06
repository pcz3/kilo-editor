// TODO: add args to main to so that clear line/screen mode can be set

/** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

typedef enum editorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
} editorKey_t;

/*** data ***/

typedef struct erow
{
    int size;
    char *chars;
} erow_t;

struct editorConfig
{
    int cx, cy;
    int screenrows;
    int screencols;
    int numrows;
    erow_t row;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    // TODO: add carriage return
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON | INPCK | ISTRIP | BRKINT);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;


    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDOUT_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDOUT_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        
        if (seq[0] == '[')
        {
            switch (seq[1])
            {
            case 'A':
                return ARROW_UP;
            case 'B':
                return ARROW_DOWN;
            case 'C':
                return ARROW_RIGHT;
            case 'D':
                return ARROW_LEFT;
            }
        }

        return '\x1b';
    }
    else    
        return c;
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int ii = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (ii < sizeof(buf) - 1)
    {
        if (read(STDOUT_FILENO, &buf[ii], 1) != 1)
            break;
        if (buf[ii] == 'R')
            break;
        ii++;
    }
    buf[ii] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** file i/o ***/

void editorOpen(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    linelen = getline(&line, &linecap, fp);

    if (linelen != -1)
    {
        while (linelen > 0 && (line[linelen - 1] == '\r' ||
                               line[linelen - 1] == '\n'))
            linelen--;
        E.row.size = linelen;
        E.row.chars = malloc(linelen + 1);
        memcpy(E.row.chars, line, linelen);

        E.row.chars[linelen] = 0;
        E.numrows = 1;
    }
    free(line);
    fclose(fp);
}

/*** append buffer ***/

typedef struct abuf
{
    char *b;
    int len;
} abuf_t;

#define ABUF_INIT {NULL, 0}

void abAppend(abuf_t *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(abuf_t *ab)
{
    free(ab->b);
}

/*** output ***/

void editorDrawRows(abuf_t *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        if (y >= E.numrows)
        {
            if (y == E.screenrows / 2)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
                abAppend(ab, "~", 1);
        }
        else
        {
            // TODO: this can be prettier
            int len = E.row.size;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, E.row.chars, len);
        }

        /*
         * Uncomment the following line to clear each terminal line
         * individually. Comment out the appending of escape sequence
         * <esc>[2j in editorRefreshScreen to prevent full screen clear
         * on every refresh loop.
         */
        abAppend(ab, "\x1b[K", 3);

        if (y < E.screenrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    abuf_t ab = ABUF_INIT;
    abAppend(&ab, "\x1b[&25l", 6);
    /*
     * Uncomment the following line to clear the entire terminal with
     * every refresh loop. Comment out the appending of escape sequence
     * <esc>[K in editorDrawRows to prevent clearing of each line
     * individually.
     */
    // abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[&25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key)
{
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
            E.cx--;
        break;
    case ARROW_RIGHT:
        if (E.cx != E.screencols - 1)
            E.cx++;
        break;
    case ARROW_DOWN:
        if (E.cy != E.screenrows - 1)
            E.cy++;
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy--;
        break;
    }
}

void editorProcessKeypress()
{
    int c = editorReadKey();
    // printf("%d\r\n", c);
    switch(c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    }
}

/*** init ***/

void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
        editorOpen(argv[1]);

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
