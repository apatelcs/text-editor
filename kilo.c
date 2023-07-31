/*** INCLUDES ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** DATA ***/

struct termios orig_termios; // Store original terminal attributes

/*** TERMINAL ***/

void die(const char *s) {
    perror(s); // Print error and s (given string)
    exit(1); // Exit with fail status
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr"); // Resets stdin to the original terminal state
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr"); // Gets stdin attribute in orig_termios struct
    atexit(disableRawMode); // Disables raw mode on exit

    struct termios raw = orig_termios; // Make copy of original terminal attributes when we enable raw mode

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // Turns off Ctrl-S and Ctrl-Q signals & turns off feature where terminal translates (C)arriage (R)eturns into (N)ew(L)ines
    raw.c_oflag &= ~(OPOST); // Turns off feature where NL turns into CR+NL
    raw.c_cflag |= ~(CS8); // Sets char size to 8 bits per byte
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // Turns off ECHO feature in raw mode (c_lflag sets local flags) & turns off ICANON (read in line by line) & turns off Ctrl-C/Z/Y/V/O signals
    raw.c_cc[VMIN] = 0; // Sets min num of bytes for read to return to 0
    raw.c_cc[VTIME] = 1; // Sets max time to wait until read returns to 1/10 sec (100 milliseconds) --> upon timeout read will return 0

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // Sets the stdin attribute after all pending output is written to terminal
}

/*** INIT ***/

int main() {
    enableRawMode(); // When starting program we enter raw mode

    while (1) {
        char c = '\0'; // Init c to 0
        
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read"); // Read one byte at a time
        
        if (iscntrl(c)) { // If c is control character (unprintable)
            printf("%d\r\n", c); // Then only print ASCII number
        } else { // Otherwise
            printf("%d ('%c')\r\n", c, c); // Print both ASCII number and character byte
        }

        if (c == 'q') break; // Exit loop if q is entered
    }

    return 0; // Return 0 at EOF
}