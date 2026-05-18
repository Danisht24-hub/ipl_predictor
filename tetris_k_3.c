#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>



#ifdef _WIN32
  #include <windows.h>
  #include <conio.h>
#else
  #include <unistd.h>
  #include <fcntl.h>
  #include <termios.h>
  #include <sys/select.h>
#endif

#define ROWS 20
#define COLS 10

// --- Platform helpers ---------------------------------------------------

#ifdef _WIN32

static void set_raw_mode(int enable) {
    (void)enable; /* conio.h handles this on Windows */
}

/* Returns 1 if a key is waiting, 0 otherwise (non-blocking). */
static int kb_hit() {
    return _kbhit();
}

/* Read one character without blocking (call only after kb_hit()==1). */
static char read_char() {
    return (char)_getch();
}

/* Sleep for ms milliseconds. */
static void sleep_ms(int ms) {
    Sleep(ms);
}

#else  /* POSIX */

static struct termios orig_termios;

static void set_raw_mode(int enable) {
    if (enable) {
        struct termios raw;
        tcgetattr(STDIN_FILENO, &orig_termios);
        raw = orig_termios;
        raw.c_lflag &= ~(ICANON | ECHO);   /* disable line-buffering & echo */
        raw.c_cc[VMIN]  = 0;               /* non-blocking read */
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    }
}

static int kb_hit() {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static char read_char() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) < 0) c = 0;
    return c;
}

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

#endif  /* _WIN32 */

// --- Game data ----------------------------------------------------------

int PIECES[7][4][2] = { // imagine it on a coordinate axis
    {{0,0},{0,1},{0,2},{0,3}},  // I
    {{0,0},{0,1},{1,0},{1,1}},  // O
    {{0,1},{1,0},{1,1},{1,2}},  // T
    {{0,1},{0,2},{1,0},{1,1}},  // S
    {{0,0},{0,1},{1,1},{1,2}},  // Z
    {{0,0},{1,0},{1,1},{1,2}},  // J
    {{0,2},{1,0},{1,1},{1,2}}   // L
};

int board[ROWS][COLS];
int piece[4][2];
int score = 0, lines = 0, level = 1;
int game_over = 0;

// --- Utility ------------------------------------------------------------

void clear_screen() {
#ifdef _WIN32
    /* Move cursor to top-left without wiping — overwrites in place, no flicker */
    COORD coord = {0, 0};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
#else
    /* Move cursor to top-left only — next printf overwrites existing text, no flash */
    printf("\033[H");
#endif
}

int is_valid(int p[4][2]) {
    int i;
    for (i = 0; i < 4; i++) {
        int r = p[i][0], c = p[i][1];
        if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return 0;
        if (board[r][c]) return 0;
    }
    return 1;
}

void spawn_piece() {
    int i, type = rand() % 7;
    for (i = 0; i < 4; i++) {
        piece[i][0] = PIECES[type][i][0];
        piece[i][1] = PIECES[type][i][1] + COLS/2 - 2;
    }
    if (!is_valid(piece))
        game_over = 1;
}

void lock_piece() {
    int i;
    for (i = 0; i < 4; i++)
        board[piece[i][0]][piece[i][1]] = 1;
}

void clear_lines() {
    int r, c, full, rr, cleared = 0;
    for (r = ROWS - 1; r >= 0; ) {
        full = 1;
        for (c = 0; c < COLS; c++)
            if (!board[r][c]) { full = 0; break; }
        if (full) {
            for (rr = r; rr > 0; rr--)
                memcpy(board[rr], board[rr-1], COLS * sizeof(int));
            memset(board[0], 0, COLS * sizeof(int));
            cleared++;
        } else {
            r--;
        }
    }
    if (cleared) {
        int pts[] = {0, 100, 300, 500, 800};
        lines += cleared;
        score += pts[cleared] * level;
        level = lines / 10 + 1;
    }
}

void draw() {
    int r, c, i, active;
    clear_screen();
    printf("\n  === TETRIS ===\n");
    printf("  Score: %d  Lines: %d  Level: %d\n\n", score, lines, level);

    printf("  +");
    for (c = 0; c < COLS; c++) printf("--");
    printf("+\n");

    for (r = 0; r < ROWS; r++) {
        printf("  |");
        for (c = 0; c < COLS; c++) {
            active = 0;
            for (i = 0; i < 4; i++)
                if (piece[i][0] == r && piece[i][1] == c) { active = 1; break; }
            if (active)           printf("[]");
            else if (board[r][c]) printf("[]");
            else                  printf("  ");
        }
        printf("|\n");
    }

    printf("  +");
    for (c = 0; c < COLS; c++) printf("--");
    printf("+\n");

    printf("\n  a=Left  d=Right  s=Down  w=Rotate  e=Drop  q=Quit\n");
    fflush(stdout);
}

void move_piece(int dr, int dc) {
    int tmp[4][2], i;
    for (i = 0; i < 4; i++) {
        tmp[i][0] = piece[i][0] + dr;
        tmp[i][1] = piece[i][1] + dc;
    }
    if (is_valid(tmp)) {
        memcpy(piece, tmp, sizeof(tmp));
    } else if (dr == 1) {
        lock_piece();
        clear_lines();
        spawn_piece();
    }
}

void rotate_piece() {
    int tmp[4][2], i, pr, pc, max_r, nr, nc;

    /* Find the top-left corner of the bounding box */
    pr = piece[0][0]; pc = piece[0][1];
    max_r = piece[0][0];
    for (i = 1; i < 4; i++) {
        if (piece[i][0] < pr)  pr    = piece[i][0];
        if (piece[i][1] < pc)  pc    = piece[i][1];
        if (piece[i][0] > max_r) max_r = piece[i][0];
    }

    /* Height of bounding box minus 1 (replaces the hardcoded 3) */
    int box_h = max_r - pr;

    /* 90-degree clockwise rotation around the bounding box:
       new_row = old_col - pc
       new_col = box_h - (old_row - pr)
       Then shift back so the top-left stays at (pr, pc). */
    for (i = 0; i < 4; i++) {
        nr = piece[i][1] - pc;
        nc = box_h - (piece[i][0] - pr);
        tmp[i][0] = nr + pr;
        tmp[i][1] = nc + pc;
    }
    if (is_valid(tmp))
        memcpy(piece, tmp, sizeof(tmp));
}

void hard_drop() {
    while (1) {
        int tmp[4][2], i;
        for (i = 0; i < 4; i++) {
            tmp[i][0] = piece[i][0] + 1;
            tmp[i][1] = piece[i][1];
        }
        if (!is_valid(tmp)) break;
        memcpy(piece, tmp, sizeof(tmp));
    }
    lock_piece();
    clear_lines();
    spawn_piece();
}

// --- Main loop ----------------------------------------------------------

int main() {
    /*
     * Timing strategy:
     *   - Loop in small 50 ms ticks.
     *   - Count ticks until drop_interval is reached, then auto-drop.
     *   - drop_interval shrinks as level increases (faster at higher levels).
     *   - Any keypress is processed immediately without waiting.
     */
    const int TICK_MS = 50;          /* poll interval */
    int ticks_elapsed = 0;

    srand((unsigned)time(NULL));
    memset(board, 0, sizeof(board));
    spawn_piece();

    /* Welcome screen – normal terminal mode */
    printf("\n  === TETRIS ===\n");
    printf("  Press Enter to start...");
    fflush(stdout);
    {
        char buf[64];
        fgets(buf, sizeof(buf), stdin);
    }

    set_raw_mode(1);   /* switch to raw / non-blocking input */

#ifndef _WIN32
    printf("\033[?25l"); /* hide cursor */
    fflush(stdout);
#else
    {   /* hide cursor on Windows */
        CONSOLE_CURSOR_INFO ci;
        GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
    }
#endif

    draw(); /* initial render before first tick */

    while (!game_over) {
        /* --- Drop interval scales with level (faster each level) --- */
        int drop_interval = (500 - (level - 1) * 40) / TICK_MS;
        if (drop_interval < 2) drop_interval = 2;  /* cap: 100 ms minimum */

        /* --- Wait one tick --- */
        sleep_ms(TICK_MS);
        ticks_elapsed++;

        /* --- Process keypress if one is waiting --- */
        if (kb_hit()) {
            char cmd = read_char();
            if      (cmd == 'a' || cmd == 'A') move_piece(0, -1);
            else if (cmd == 'd' || cmd == 'D') move_piece(0, +1);
            else if (cmd == 's' || cmd == 'S') move_piece(+1, 0);
            else if (cmd == 'w' || cmd == 'W') rotate_piece();
            else if (cmd == 'e' || cmd == 'E') hard_drop();
            else if (cmd == 'q' || cmd == 'Q') { game_over = 1; break; }
        }

        /* --- Auto-drop every drop_interval ticks --- */
        if (ticks_elapsed >= drop_interval) {
            move_piece(+1, 0);
            ticks_elapsed = 0;
        }

        /* --- Draw AFTER all state changes so new piece appears instantly --- */
        draw();
    }

    set_raw_mode(0);   /* restore terminal */

#ifndef _WIN32
    printf("\033[?25h"); /* restore cursor */
    fflush(stdout);
#else
    {
        CONSOLE_CURSOR_INFO ci;
        GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
        ci.bVisible = TRUE;
        SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
    }
#endif

    draw();
    printf("\n  --- GAME OVER ---\n");
    printf("  Final Score : %d\n", score);
    printf("  Lines       : %d\n", lines);
    printf("  Level       : %d\n\n", level);

    return 0;
}
