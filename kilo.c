/*** INCLUDES ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** DEFINES ***/

#define KILO_VERSION "0.0.1" // Defines editor version
#define CTRL_KEY(k) ((k) &0x1f) // Defines Ctrl macro

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

/*** DATA ***/

struct editorConfig {
    int cx, cy; // Cursor x and y pos
    int screenrows; // Editor rows (height)
    int screencols; // Editor cols (width)
    struct termios orig_termios; // Store original terminal attributes
};

struct editorConfig E; // Struct holding info about editor state


/*** TERMINAL ***/

void die(const char *s) {
    // Clear screen first
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s); // Print error and s (given string)
    exit(1); // Exit with fail status
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr"); // Resets stdin to the original terminal state
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // Gets stdin attribute in orig_termios struct
    atexit(disableRawMode); // Disables raw mode on exit

    struct termios raw = E.orig_termios; // Make copy of original terminal attributes when we enable raw mode

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // Turns off Ctrl-S and Ctrl-Q signals & turns off feature where terminal translates (C)arriage (R)eturns into (N)ew(L)ines
    raw.c_oflag &= ~(OPOST); // Turns off feature where NL turns into CR+NL
    raw.c_cflag |= ~(CS8); // Sets char size to 8 bits per byte
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // Turns off ECHO feature in raw mode (c_lflag sets local flags) & turns off ICANON (read in line by line) & turns off Ctrl-C/Z/Y/V/O signals
    raw.c_cc[VMIN] = 0; // Sets min num of bytes for read to return to 0
    raw.c_cc[VTIME] = 1; // Sets max time to wait until read returns to 1/10 sec (100 milliseconds) --> upon timeout read will return 0

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // Sets the stdin attribute after all pending output is written to terminal
}

int editorReadKey() {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) { // Read one byte at a time
        if (nread == -1 && errno != EAGAIN) die("read"); // Error handling
    }

    if (c == '\x1b') { // If we read esc
        char seq[3];

        // If nothing after esc then return esc
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') { // If we have esc sequence then remap arrow keys to our enum
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }

        return '\x1b'; // Default to returning esc
    } else { // If not esc then return the char itself
        return c;
    }
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1; // If reading window size failed or get 0 columns then we have an error
    } else {
        *cols = ws.ws_col; // Otherwise set num of cols and rows with success
        *rows = ws.ws_row;
        return 0;
    }
}

/*** INPUT ***/

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT: // h moves cursor left
            if (E.cx != 0) E.cx--;
            break;
        case ARROW_RIGHT: // l moves cursor right
            if (E.cx != E.screencols - 1) E.cx++;
            break;
        case ARROW_UP: // k moves cursor up
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_DOWN: // j moves cursor down
            if (E.cy != E.screenrows - 1) E.cy++;
            break;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey(); // Read character

    switch (c) {
        case CTRL_KEY('q'): // If Ctrl-q then exit program
            // Clear screen first
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);

            // Then close prog with success
            exit(0);
            break;
        case ARROW_DOWN: // Cursor position keys
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
            editorMoveCursor(c);
            break;
    }
}

/*** APPEND BUFFER ***/

struct abuf {
    char *b; // String
    int len; // Length of append buffer
};

#define ABUF_INIT {NULL, 0} // Defines empty append buffer

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len); // Reallocate new amount of memory for our desired string at same place in memory

    if (new == NULL) return; // If our new char ptr is null then we are done
    
    memcpy(&new[ab->len], s, len); // Copy string s into memory at new char ptr
    ab->b = new; // Update string
    ab->len += len; // Update length
}

void abFree(struct abuf *ab) {
    free(ab->b); // Deallocate memory
}

/*** OUTPUT ***/

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) { // For each row on the screen
        if (y == E.screenrows / 3) { // If we are a third of the way down the screen
            char welcome[80];
            int welcomeLen = snprintf(welcome, sizeof(welcome), "Kilo Editor -- version %s", KILO_VERSION); // Set welcome message

            if (welcomeLen > E.screencols) welcomeLen = E.screencols; // If message is longer than screenwidth cut the message short

            int padding = (E.screencols - welcomeLen) / 2; // Get padding amount
            if (padding) {
                abAppend(ab, "~", 1); // If any padding needed place tilde at beginning of line
                padding--; // Decrement padding
            }
            while (padding--) abAppend(ab, " ", 1); // Pad welcome message to center it

            abAppend(ab, welcome, welcomeLen); // Put welcome message onto append buffer
        } else {
            abAppend(ab, "~", 1); // Write ~ on each line
        }

        abAppend(ab, "\x1b[K", 3); // Erase everything to the right of the cursor on curr line
        if (y < E.screenrows - 1)
            abAppend(ab, "\r\n", 2); // If not the last line write CR+NL
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT; // Empty append buffer

    abAppend(&ab, "\x1b[?25l", 6); // Hide cursor before refreshing screen
    abAppend(&ab, "\x1b[H", 3); // Writing 3 bytes to append buffer (H : cursor position command, defaults to 1;1 which is first row/col)

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1); // Set buf string as cursor at stored position
    abAppend(&ab, buf, strlen(buf)); // Write cursor to buffer
    abAppend(&ab, "\x1b[?25h", 6); // Show cursor

    write(STDOUT_FILENO, ab.b, ab.len); // Write append buffer to terminal
    abFree(&ab); // Free memory
}

/*** INIT ***/

void initEditor() {
    E.cx = 0; // Init cursor pos to top left
    E.cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize"); // Inits screen size
}

int main() {
    enableRawMode(); // When starting program we enter raw mode
    initEditor(); // Inits editor settings

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress(); // Function call
    }

    return 0; // Return 0 at EOF
}
