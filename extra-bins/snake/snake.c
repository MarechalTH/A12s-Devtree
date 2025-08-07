#include <stddef.h> 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/select.h>

// === CONSTANTES Y ESTRUCTURAS ===
#define MAX_SNAKE 1000
#define MAX_FRUITS 10
#define MAX_WALLS 75
#define MAX_BUGS 5
#define MAX_POWERUPS 3
#define LOGIC_DELAY_US 1000
#define FRAME_EVERY_N_TICKS 100
#define HEAD_INDEX 0
#define SAFE_ZONE_RADIUS 5

#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef enum { UP, DOWN, LEFT, RIGHT } Direction;
typedef enum {
    RAT_ERRATIC,
    RAT_STALKER,
    RAT_AMBUSHER,
    RAT_PANIC,
    RAT_PACK_HUNTER,
    RAT_TRAP_MASTER,
    RAT_U_DODGER,
    RAT_AGGRESSIVE,
    RAT_COUNT
} RatMode;
typedef enum { INVULNERABILITY, SPEED_BOOST, SLOW_BUGS, AREA_EXPLOSION } PowerUpType;

typedef struct {
    int x[MAX_SNAKE];
    int y[MAX_SNAKE];
    int length;
    Direction dir;
} Snake;

typedef struct {
    int x;
    int y;
    bool active;
} Fruit;

typedef struct {
    int x;
    int y;
} Wall;

typedef struct {
    int x;
    int y;
    bool active;
    Direction dir;
    int move_counter;
    RatMode mode;
    RatMode previous_mode;
    int mode_timer;
    int target_fruit;
    int partner_index;
} Bug;

typedef struct {
    int x;
    int y;
    bool active;
    PowerUpType type;
    time_t spawn_time;
} PowerUp;

typedef struct {
    int total_time;
    int fruits_eaten;
    int max_length;
    int bugs_killed;
    int bugs_by_type[RAT_COUNT];
    int direction_changes;
    int powerups_collected;
} GameStats;

typedef enum { SYMBOL_SET_UNICODE, SYMBOL_SET_ASCII } SymbolSet;

typedef struct {
    const char* head;
    const char* body_horizontal;
    const char* body_vertical;
    const char* body_top_right;
    const char* body_top_left;
    const char* body_bottom_right;
    const char* body_bottom_left;
    const char* fruit;
    const char* wall;
    const char* bug;
    const char* border_top_left;
    const char* border_top_right;
    const char* border_bottom_left;
    const char* border_bottom_right;
    const char* border_horizontal;
    const char* border_vertical;
    const char* powerup_invulnerability;
    const char* powerup_speed;
    const char* powerup_slow;
    const char* powerup_explosion;
    const char* collision_segment;
} GameSymbols;

// === GLOBAL VARS ===
static struct termios orig_termios;
static int WIDTH, HEIGHT;
static int Y_OFFSET = 9;
static time_t start_time;

static int pending_growth = 0;
static Fruit fruits[MAX_FRUITS];
static int fruit_count = 0;
static Wall walls[MAX_WALLS];
static int wall_count = 0;
static int fruits_eaten = 0;
static Bug bugs[MAX_BUGS];
static int active_bugs = 0;
static int bugs_killed_by_body = 0;
static PowerUp powerups[MAX_POWERUPS];
static int active_powerups = 0;
static bool is_invulnerable = false;
static int invulnerability_timer = 0;
static bool speed_boost_active = false;
static int speed_boost_timer = 0;
static bool slow_bugs_active = false;
static int slow_bugs_timer = 0;
static GameStats game_stats;
static int last_direction = -1;
static bool debug_mode = false;
static bool is_valid_direction(Direction new_dir);

static Snake snake;
static GameSymbols symbols;
static SymbolSet current_symbol_set = SYMBOL_SET_UNICODE;

// === FUNCTIONS FOR TERMINAL AND RENDER ===
static void move_cursor(int x, int y) {
    printf("\x1b[%d;%dH", y + 1 + Y_OFFSET, x + 1);
    fflush(stdout);
}

static void hide_cursor() {
    printf("\x1b[?25l");
    fflush(stdout);
}

static void show_cursor() {
    printf("\x1b[?25h");
    fflush(stdout);
}

static void draw_segment(int x, int y, const char* c) {
    move_cursor(x, y);
    printf("%s", c);
    fflush(stdout);
}

static void clear_segment(int x, int y) {
    move_cursor(x, y);
    printf("  ");
    fflush(stdout);
}

static void clear_game_area() {
    for (int y = 0; y <= HEIGHT; y++) {
        move_cursor(0, y);
        for (int x = 0; x <= WIDTH; x++) {
            putchar(' ');
        }
    }
    fflush(stdout);
}

static void set_snake_color() { printf("\x1b[38;5;156m"); }
static void set_fruit_color() { printf("\x1b[38;5;217m"); }
static void set_wall_color() { printf("\x1b[38;5;180m"); }
static void set_border_color() { printf("\x1b[38;5;223m"); }

static void set_bug_color(RatMode mode) {
    switch (mode) {
        case RAT_ERRATIC:     printf("\x1b[38;5;75m"); break;
        case RAT_STALKER:     printf("\x1b[38;5;166m"); break;
        case RAT_AMBUSHER:    printf("\x1b[38;5;124m"); break;
        case RAT_PANIC:       printf("\x1b[38;5;178m"); break;
        case RAT_PACK_HUNTER: printf("\x1b[38;5;36m"); break;
        case RAT_TRAP_MASTER: printf("\x1b[38;5;55m"); break;
        case RAT_U_DODGER:    printf("\x1b[38;5;15m"); break;
        case RAT_AGGRESSIVE:  printf("\x1b[38;5;88m"); break;
        default:              printf("\x1b[38;5;153m"); break;
    }
}

static void reset_color() { printf("\x1b[0m"); }

static void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    show_cursor();
    reset_color();
}

static void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    hide_cursor();
}

static int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

static char getch() {
    char c;
    return (read(STDIN_FILENO, &c, 1) == 1) ? c : 0;
}

static int read_key() {
    char c = getch();
    if (c == 0) return 0;

    if (c == 27) {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) == 0) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) == 0) return 27;

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return 'w';
                case 'B': return 's';
                case 'C': return 'd';
                case 'D': return 'a';
            }
        }
        return 27;
    }
     if (is_invulnerable) {
        Direction new_dir = snake.dir;
        switch (c) {
            case 'w': case 'W': new_dir = UP; break;
            case 's': case 'S': new_dir = DOWN; break;
            case 'a': case 'A': new_dir = LEFT; break;
            case 'd': case 'D': new_dir = RIGHT; break;
            default: return c;
        }
        
        if (is_valid_direction(new_dir)) {
            snake.dir = new_dir;
        } else {
            printf("\a"); fflush(stdout);
        }
        return 0;
    } else {
        return c;
    }
}

static void get_terminal_size() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    WIDTH = w.ws_col - 2;
    HEIGHT = w.ws_row - Y_OFFSET - 1;

    if (w.ws_row < Y_OFFSET + 10) {
        fprintf(stderr, "Terminal too small - needs at least %d lines. Actual: %d\n", Y_OFFSET + 10, w.ws_row);
        exit(1);
    } else {
        HEIGHT = w.ws_row - 11;
    }

    if (WIDTH < 40) WIDTH = 40;
    if (HEIGHT < 10) HEIGHT = 10;
}

static void init_symbols(SymbolSet set) {
    current_symbol_set = set;
    
    if (set == SYMBOL_SET_UNICODE) {
        symbols.head = "●";
        symbols.body_horizontal = "─";
        symbols.body_vertical = "│";
        symbols.body_top_right = "┌";
        symbols.body_top_left = "┐";
        symbols.body_bottom_right = "└";
        symbols.body_bottom_left = "┘";
        symbols.fruit = "●";
        symbols.wall = "█";
        symbols.bug = "⬤";
        symbols.border_top_left = "╭";
        symbols.border_top_right = "╮";
        symbols.border_bottom_left = "╰";
        symbols.border_bottom_right = "╯";
        symbols.border_horizontal = "═";
        symbols.border_vertical = "│";
        symbols.powerup_invulnerability = "🛡️ ";
        symbols.powerup_speed = "⚡ ";
        symbols.powerup_slow = "🐌 ";
        symbols.powerup_explosion = "💥 ";
        symbols.collision_segment = "◈";
    } else {
        symbols.head = "@";
        symbols.body_horizontal = "-";
        symbols.body_vertical = "|";
        symbols.body_top_right = "/";
        symbols.body_top_left = "\\";
        symbols.body_bottom_right = "\\";
        symbols.body_bottom_left = "/";
        symbols.fruit = "*";
        symbols.wall = "#";
        symbols.bug = "X";
        symbols.border_top_left = "+";
        symbols.border_top_right = "+";
        symbols.border_bottom_left = "+";
        symbols.border_bottom_right = "+";
        symbols.border_horizontal = "-";
        symbols.border_vertical = "|";
        symbols.powerup_invulnerability = "I ";
        symbols.powerup_speed = "S ";
        symbols.powerup_slow = "L ";
        symbols.powerup_explosion = "E ";
        symbols.collision_segment = "#";
    }
}

static void draw_border() {
    set_border_color();
    
    move_cursor(0, 0);
    printf("%s", symbols.border_top_left);
    for (int x = 1; x < WIDTH; x++) printf("%s", symbols.border_horizontal);
    printf("%s", symbols.border_top_right);
    
    for (int y = 1; y < HEIGHT; y++) {
        move_cursor(0, y);
        printf("%s", symbols.border_vertical);
        move_cursor(WIDTH, y);
        printf("%s", symbols.border_vertical);
    }
    
    move_cursor(0, HEIGHT);
    printf("%s", symbols.border_bottom_left);
    for (int x = 1; x < WIDTH; x++) printf("%s", symbols.border_horizontal);
    printf("%s", symbols.border_bottom_right);
    
    reset_color();
    fflush(stdout);
}

static const char* get_head_char() {
    return symbols.head;
}

static const char* get_body_char(int idx) {
    if (idx < 0 || idx >= snake.length) return " ";

    if (idx == snake.length - 1) {
        if (idx > 0) {
            int dx = snake.x[idx - 1] - snake.x[idx];
            int dy = snake.y[idx - 1] - snake.y[idx];
            if (dx != 0) return symbols.body_horizontal;
            if (dy != 0) return symbols.body_vertical;
        }
        return " ";
    }

    if (idx == HEAD_INDEX) {
        return get_head_char();
    }

    int dx_prev = snake.x[idx] - snake.x[idx-1];
    int dy_prev = snake.y[idx] - snake.y[idx-1];
    Direction in_dir;
    if (dx_prev == 1)       in_dir = RIGHT;
    else if (dx_prev == -1) in_dir = LEFT;
    else if (dy_prev == 1)  in_dir = DOWN;
    else                    in_dir = UP;

    int dx_next = snake.x[idx+1] - snake.x[idx];
    int dy_next = snake.y[idx+1] - snake.y[idx];
    Direction out_dir;
    if (dx_next == 1)       out_dir = RIGHT;
    else if (dx_next == -1) out_dir = LEFT;
    else if (dy_next == 1)  out_dir = DOWN;
    else                    out_dir = UP;

    if (in_dir == out_dir) {
        return (in_dir == UP || in_dir == DOWN) ? symbols.body_vertical : symbols.body_horizontal;
    }
    else if ((in_dir == UP && out_dir == RIGHT) || (in_dir == LEFT && out_dir == DOWN)) {
        return symbols.body_top_right;
    }
    else if ((in_dir == UP && out_dir == LEFT) || (in_dir == RIGHT && out_dir == DOWN)) {
        return symbols.body_top_left;
    }
    else if ((in_dir == DOWN && out_dir == RIGHT) || (in_dir == LEFT && out_dir == UP)) {
        return symbols.body_bottom_right;
    }
    else if ((in_dir == DOWN && out_dir == LEFT) || (in_dir == RIGHT && out_dir == UP)) {
        return symbols.body_bottom_left;
    }

    return " ";
}

static void draw_score_time(int score, int elapsed) {
    move_cursor(1, HEIGHT + 1);
    printf("Time: %3ds  Score: %d  Walls: %d  Bugs: %d", elapsed, score, wall_count, active_bugs);
    
    move_cursor(WIDTH - 15, HEIGHT + 1);
    if (is_invulnerable) printf("%s", symbols.powerup_invulnerability);
    if (speed_boost_active) printf("%s", symbols.powerup_speed);
    if (slow_bugs_active) printf("%s", symbols.powerup_slow);
    
    printf("\x1b[K");
    fflush(stdout);
}

static void draw_fruits() {
    set_fruit_color();
    for (int i = 0; i < fruit_count; i++) {
        if (fruits[i].active) {
            draw_segment(fruits[i].x, fruits[i].y, symbols.fruit);
        }
    }
    reset_color();
}

static void draw_walls() {
    set_wall_color();
    for (int i = 0; i < wall_count; i++) {
        draw_segment(walls[i].x, walls[i].y, symbols.wall);
    }
    reset_color();
}

static void draw_bugs() {
    for (int i = 0; i < MAX_BUGS; i++) {
        if (bugs[i].active) {
            set_bug_color(bugs[i].mode);
            draw_segment(bugs[i].x, bugs[i].y, symbols.bug);
            reset_color();
        }
    }
}

static void draw_powerups() {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active) {
            move_cursor(powerups[i].x, powerups[i].y);
            switch (powerups[i].type) {
                case INVULNERABILITY: printf("%s", symbols.powerup_invulnerability); break;
                case SPEED_BOOST:     printf("%s", symbols.powerup_speed); break;
                case SLOW_BUGS:       printf("%s", symbols.powerup_slow); break;
                case AREA_EXPLOSION:  printf("%s", symbols.powerup_explosion); break;
            }
        }
    }
    fflush(stdout);
}

// === OBJECTS GENERATION ===
static bool is_position_valid(int x, int y) {
    if (x <= 0 || x >= WIDTH || y <= 0 || y >= HEIGHT) return false;
    
    for (int j = 0; j < snake.length; j++) {
        if (snake.x[j] == x && snake.y[j] == y) return false;
    }
    
    for (int j = 0; j < wall_count; j++) {
        if (walls[j].x == x && walls[j].y == y) return false;
    }
    
    for (int j = 0; j < MAX_BUGS; j++) {
        if (bugs[j].active && bugs[j].x == x && bugs[j].y == y) return false;
    }
    
    for (int j = 0; j < MAX_POWERUPS; j++) {
        if (powerups[j].active && powerups[j].x == x && powerups[j].y == y) return false;
    }
    
    return true;
}

static void generate_fruit_at(int i) {
    int x, y;
    int attempts = 0;
    do {
        x = (rand() % (WIDTH - 2)) + 1;
        y = (rand() % (HEIGHT - 2)) + 1;
        attempts++;
        if (attempts > 100) {
            fruits[i].active = false;
            return;
        }
    } while (!is_position_valid(x, y));

    fruits[i].x = x;
    fruits[i].y = y;
    fruits[i].active = true;
}

static void generate_fruits() {
    int area = (WIDTH - 2) * (HEIGHT - 2);
    fruit_count = (area > 1500) ? 10 : 5;

    for (int i = 0; i < fruit_count; i++) {
        generate_fruit_at(i);
    }
}

static void generate_wall() {
    if (wall_count >= MAX_WALLS) return;

    int x, y;
    int attempts = 0;
    do {
        x = (rand() % (WIDTH - 2)) + 1;
        y = (rand() % (HEIGHT - 2)) + 1;
        attempts++;
        if (attempts > 100) return;
    } while (!is_position_valid(x, y) || (abs(x - snake.x[0]) + abs(y - snake.y[0]) < SAFE_ZONE_RADIUS));

    walls[wall_count].x = x;
    walls[wall_count].y = y;
    wall_count++;
    draw_walls();
}

static void spawn_bug() {
    if (active_bugs >= MAX_BUGS) return;
    
    int x, y;
    int attempts = 0;
    for (int i = 0; i < MAX_BUGS; i++) {
        if (!bugs[i].active) {
            do {
                x = (rand() % (WIDTH - 2)) + 1;
                y = (rand() % (HEIGHT - 2)) + 1;
                attempts++;
                if (attempts > 50) break;
            } while (!is_position_valid(x, y) || (abs(x - snake.x[0]) + abs(y - snake.y[0]) < SAFE_ZONE_RADIUS));

            if (attempts > 50) break;

            bugs[i].x = x;
            bugs[i].y = y;
            bugs[i].active = true;
            bugs[i].dir = (Direction)(rand() % 4);
            bugs[i].move_counter = 0;
            bugs[i].mode = (RatMode)(rand() % RAT_COUNT);
            bugs[i].previous_mode = bugs[i].mode;
            bugs[i].mode_timer = 20 + (rand() % 30);
            active_bugs++;
            break;
        }
    }
}

static void spawn_powerup() {
    if (active_powerups >= MAX_POWERUPS) return;
    
    int x, y;
    int attempts = 0;
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) {
            do {
                x = (rand() % (WIDTH - 2)) + 1;
                y = (rand() % (HEIGHT - 2)) + 1;
                attempts++;
                if (attempts > 50) return;
            } while (!is_position_valid(x, y) || (abs(x - snake.x[0]) + abs(y - snake.y[0]) < SAFE_ZONE_RADIUS));

            powerups[i].x = x;
            powerups[i].y = y;
            powerups[i].active = true;
            powerups[i].type = (PowerUpType)(rand() % 4);
            powerups[i].spawn_time = time(NULL);
            active_powerups++;
            break;
        }
    }
}

// === SNAKE LOGIC ===
static void move_snake() {
    static bool blocked = false;
    static int last_collision_x = -1, last_collision_y = -1;

    if (blocked && is_valid_direction(snake.dir)) {
        if(last_collision_x != -1){
            bool is_wall = false;
            for(int i = 0; i < wall_count; i++){
                if(walls[i].x == last_collision_x && walls[i].y == last_collision_y){
                    is_wall = true;
                    break;
                }
            }
            if(is_wall){
                set_wall_color();
                draw_segment(last_collision_x, last_collision_y, symbols.wall);
            } else {
                 set_border_color();
                const char* border_char = " ";
                if (last_collision_y == 0) border_char = symbols.border_horizontal;
                if (last_collision_y == HEIGHT) border_char = symbols.border_horizontal;
                if (last_collision_x == 0) border_char = symbols.border_vertical;
                if (last_collision_x == WIDTH) border_char = symbols.border_vertical;
                if(last_collision_x == 0 && last_collision_y == 0) border_char = symbols.border_top_left;
                if(last_collision_x == WIDTH && last_collision_y == 0) border_char = symbols.border_top_right;
                if(last_collision_x == 0 && last_collision_y == HEIGHT) border_char = symbols.border_bottom_left;
                if(last_collision_x == WIDTH && last_collision_y == HEIGHT) border_char = symbols.border_bottom_right;
                draw_segment(last_collision_x, last_collision_y, border_char);
            }
            reset_color();
        }
        blocked = false;
        last_collision_x = -1;
        last_collision_y = -1;
    }

    int tail_x = snake.x[snake.length - 1];
    int tail_y = snake.y[snake.length - 1];
    
    int new_x = snake.x[0];
    int new_y = snake.y[0];
    switch (snake.dir) {
        case UP:    new_y--; break;
        case DOWN:  new_y++; break;
        case LEFT:  new_x--; break;
        case RIGHT: new_x++; break;
    }

    bool collision = false;
    if (new_x <= 0 || new_x >= WIDTH || new_y <= 0 || new_y >= HEIGHT) {
        collision = true;
    }
    for (int i = 0; i < wall_count && !collision; i++) {
        if (walls[i].x == new_x && walls[i].y == new_y) {
            collision = true;
        }
    }

    if (collision) {
        if (!is_invulnerable) {
            snake.x[0] = new_x;
            snake.y[0] = new_y;
            return;
        } else {
            blocked = true;
            last_collision_x = new_x;
            last_collision_y = new_y;
            set_snake_color();
            draw_segment(last_collision_x, last_collision_y, symbols.collision_segment);
            reset_color();
            printf("\a"); fflush(stdout);
            return;
        }
    }

    for (int i = snake.length - 1; i > 0; i--) {
        snake.x[i] = snake.x[i - 1];
        snake.y[i] = snake.y[i - 1];
    }
    snake.x[0] = new_x;
    snake.y[0] = new_y;

    if (pending_growth > 0 && snake.length < MAX_SNAKE) {
        snake.length++;
        snake.x[snake.length - 1] = tail_x;
        snake.y[snake.length - 1] = tail_y;
        pending_growth--;
    } else {
        clear_segment(tail_x, tail_y);
    }

    set_snake_color();
    draw_segment(snake.x[0], snake.y[0], get_head_char());
    for (int i = 1; i < snake.length; i++) {
        draw_segment(snake.x[i], snake.y[i], get_body_char(i));
    }
    reset_color();
}

static bool is_valid_direction(Direction new_dir) {
    int new_x = snake.x[0], new_y = snake.y[0];
    switch (new_dir) {
        case UP:    new_y--; break;
        case DOWN:  new_y++; break;
        case LEFT:  new_x--; break;
        case RIGHT: new_x++; break;
    }
    if (new_x <= 0 || new_x >= WIDTH || new_y <= 0 || new_y >= HEIGHT) return false;
    for (int i = 0; i < wall_count; i++) {
        if (walls[i].x == new_x && walls[i].y == new_y) return false;
    }
    return true;
}

static bool is_head_vulnerable() {
    if (snake.length <= 1) return true;
    int head_x = snake.x[0], head_y = snake.y[0];
    int adj[4][2] = {{0,1}, {1,0}, {0,-1}, {-1,0}};
    int count = 0;
    for(int i = 0; i < 4; i++){
        int nx = head_x + adj[i][0];
        int ny = head_y + adj[i][1];
        for(int j = 1; j < snake.length; j++){
            if(snake.x[j] == nx && snake.y[j] == ny){
                count++;
                break;
            }
        }
    }
    return (count < 2);
}

static void destroy_wall(int x, int y) {
    for (int i = 0; i < wall_count; i++) {
        if (walls[i].x == x && walls[i].y == y) {
            clear_segment(x, y);
            walls[i] = walls[wall_count - 1];
            wall_count--;
            break;
        }
    }
}

// === BUGS AI ===
static bool detect_u_pattern() {
    if (snake.length < 5) return false;
    int direction_changes = 0;
    Direction last_dir = snake.dir;
    for (int i = 1; i < 5 && i < snake.length; i++) {
        int dx = snake.x[i-1] - snake.x[i];
        int dy = snake.y[i-1] - snake.y[i];
        Direction seg_dir;
        if (dx == 1) seg_dir = RIGHT;
        else if (dx == -1) seg_dir = LEFT;
        else if (dy == 1) seg_dir = DOWN;
        else seg_dir = UP;
        if (seg_dir != last_dir) {
            direction_changes++;
            last_dir = seg_dir;
        }
    }
    return direction_changes >= 2;
}

static void find_strategic_fruit(Bug* bug) {
    int best_fruit = -1;
    int min_dist = INT_MAX;
    for (int i = 0; i < fruit_count; i++) {
        if (!fruits[i].active) continue;
        int snake_dist = abs(snake.x[0] - fruits[i].x) + abs(snake.y[0] - fruits[i].y);
        int rat_dist = abs(bug->x - fruits[i].x) + abs(bug->y - fruits[i].y);
        if (snake_dist < 10 && rat_dist < min_dist) {
            best_fruit = i;
            min_dist = rat_dist;
        }
    }
    bug->target_fruit = best_fruit;
}

static bool coordinate_with_partner(Bug* bug) {
    if (bug->partner_index < 0 || bug->partner_index >= MAX_BUGS || !bugs[bug->partner_index].active) return false;
    
    Bug* partner = &bugs[bug->partner_index];
    int dx = snake.x[0] - bug->x;
    int dy = snake.y[0] - bug->y;
    int pdx = snake.x[0] - partner->x;
    int pdy = snake.y[0] - partner->y;
    
    if ((dx * pdx < 0 && dy * pdy < 0) || (abs(dx) > WIDTH/3 && abs(pdx) > WIDTH/3)) {
        if (rand() % 2 == 0) {
            bug->dir = (dx > 0) ? RIGHT : LEFT;
            partner->dir = (dy > 0) ? DOWN : UP;
        } else {
            bug->dir = (dy > 0) ? DOWN : UP;
            partner->dir = (dx > 0) ? RIGHT : LEFT;
        }
        return true;
    }
    return false;
}

static void set_trap_for_snake(Bug* bug) {
    int closest_wall = -1;
    int min_dist = INT_MAX;
    for (int i = 0; i < wall_count; i++) {
        int dist = abs(walls[i].x - snake.x[0]) + abs(walls[i].y - snake.y[0]);
        if (dist < 10 && dist < min_dist) {
            closest_wall = i;
            min_dist = dist;
        }
    }
    if (closest_wall >= 0) {
        int wx = walls[closest_wall].x, wy = walls[closest_wall].y;
        int dx = wx - snake.x[0], dy = wy - snake.y[0];
        bug->dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? LEFT : RIGHT) : ((dy > 0) ? UP : DOWN);
    } else {
        int dx = snake.x[0] - bug->x, dy = snake.y[0] - bug->y;
        bug->dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? RIGHT : LEFT) : ((dy > 0) ? DOWN : UP);
    }
}


static void move_bugs() {
    for (int i = 0; i < MAX_BUGS; i++) {
        if (!bugs[i].active) continue;

        RatMode previous_mode = bugs[i].mode;
        
        if (bugs[i].mode_timer-- <= 0) {
            int mode_weights[RAT_COUNT] = {20, 20, 15, 10, 10, 5, 5, 15};
            if (fruits_eaten > 20) {
                mode_weights[RAT_AMBUSHER] += 5;
                mode_weights[RAT_AGGRESSIVE] += 10;
            }
            if (wall_count > 10) mode_weights[RAT_TRAP_MASTER] += 5;
            if (active_bugs > 1) mode_weights[RAT_PACK_HUNTER] += 10;

            int total_weight = 0;
            for(int j=0; j<RAT_COUNT; ++j) total_weight += mode_weights[j];
            
            int r = rand() % total_weight;
            int new_mode_idx = 0;
            while (r >= mode_weights[new_mode_idx]) {
                r -= mode_weights[new_mode_idx];
                new_mode_idx++;
            }
            bugs[i].mode = (RatMode)new_mode_idx;
            bugs[i].mode_timer = 50 + (rand() % 100);

            if (bugs[i].mode == RAT_PANIC && previous_mode != RAT_PANIC && wall_count < MAX_WALLS) {
                if (is_position_valid(bugs[i].x, bugs[i].y)) {
                    walls[wall_count].x = bugs[i].x;
                    walls[wall_count].y = bugs[i].y;
                    wall_count++;
                    set_wall_color();
                    draw_segment(bugs[i].x, bugs[i].y, symbols.wall);
                    reset_color();
                }
            }
            
            switch (bugs[i].mode) {
                case RAT_AMBUSHER: find_strategic_fruit(&bugs[i]); break;
                case RAT_PACK_HUNTER:
                    bugs[i].partner_index = -1;
                    for (int j = 0; j < MAX_BUGS; j++) {
                        if (j != i && bugs[j].active && bugs[j].mode != RAT_PACK_HUNTER) {
                            bugs[i].partner_index = j;
                            break;
                        }
                    }
                    break;
                default: break;
            }
        }

        int new_x = bugs[i].x, new_y = bugs[i].y;
        bool force_move = false;
        int dx = 0, dy = 0, fx = 0, fy = 0;
        
        switch (bugs[i].mode) {
             case RAT_ERRATIC:
                if (rand() % 5 == 0) bugs[i].dir = (Direction)(rand() % 4);
                if (rand() % 3 == 0) {
                    dx = snake.x[0] - bugs[i].x;
                    dy = snake.y[0] - bugs[i].y;
                    if (abs(dx) > abs(dy)) bugs[i].dir = (dx > 0) ? RIGHT : LEFT;
                    else if (dy != 0) bugs[i].dir = (dy > 0) ? DOWN : UP;
                }
                break;
            case RAT_STALKER:
                dx = snake.x[0] - bugs[i].x;
                dy = snake.y[0] - bugs[i].y;
                if (is_head_vulnerable() && (abs(dx) + abs(dy) <= 10)) {
                    bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? RIGHT : LEFT) : ((dy > 0) ? DOWN : UP);
                    force_move = true;
                } else {
                    bool avoid_segment = false;
                    for (int j = 1; j < snake.length; j++) {
                        if (abs(snake.x[j] - bugs[i].x) <= 2 && abs(snake.y[j] - bugs[i].y) <= 2) {
                            avoid_segment = true;
                            if (snake.x[j] < bugs[i].x && dx > -3) dx += 3;
                            if (snake.x[j] > bugs[i].x && dx < 3) dx -= 3;
                            if (snake.y[j] < bugs[i].y && dy > -3) dy += 3;
                            if (snake.y[j] > bugs[i].y && dy < 3) dy -= 3;
                        }
                    }
                    if (!avoid_segment) {
                        if (abs(dx) + abs(dy) < 5) bugs[i].dir = (rand() % 2 == 0) ? ((dx > 0) ? LEFT : RIGHT) : ((dy > 0) ? UP : DOWN);
                        else if (abs(dx) > abs(dy)) bugs[i].dir = (dx > 0) ? RIGHT : LEFT;
                        else if (dy != 0) bugs[i].dir = (dy > 0) ? DOWN : UP;
                    }
                }
                break;
            case RAT_U_DODGER:
                dx = snake.x[0] - bugs[i].x;
                dy = snake.y[0] - bugs[i].y;
                if (detect_u_pattern() || (abs(dx) + abs(dy) < 8)) {
                    bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? LEFT : RIGHT) : ((dy > 0) ? UP : DOWN);
                } else {
                    bugs[i].dir = (Direction)(rand() % 4);
                }
                break;
            case RAT_AMBUSHER:
                if (bugs[i].target_fruit >= 0 && bugs[i].target_fruit < fruit_count && fruits[bugs[i].target_fruit].active) {
                    fx = fruits[bugs[i].target_fruit].x;
                    fy = fruits[bugs[i].target_fruit].y;
                    dx = fx - bugs[i].x; dy = fy - bugs[i].y;
                    bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? RIGHT : LEFT) : ((dy > 0) ? DOWN : UP);
                    if (abs(dx) + abs(dy) < 4) {
                        dx = snake.x[0] - bugs[i].x; dy = snake.y[0] - bugs[i].y;
                        bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? RIGHT : LEFT) : ((dy > 0) ? DOWN : UP);
                        force_move = true;
                    }
                } else {
                    find_strategic_fruit(&bugs[i]);
                }
                break;
            case RAT_PANIC:
                dx = snake.x[0] - bugs[i].x; dy = snake.y[0] - bugs[i].y;
                bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? LEFT : RIGHT) : ((dy > 0) ? UP : DOWN);
                break;
            case RAT_PACK_HUNTER:
                if (coordinate_with_partner(&bugs[i])) force_move = true;
                else bugs[i].mode_timer = 0;
                break;
            case RAT_TRAP_MASTER:
                set_trap_for_snake(&bugs[i]);
                break;
            case RAT_AGGRESSIVE: {
                bool can_attack_head = false;
                for (int j = 0; j < 3 && j < snake.length; j++) {
                    if (abs(snake.x[j] - bugs[i].x) + abs(snake.y[j] - bugs[i].y) <= 5) {
                        dx = snake.x[j] - bugs[i].x; dy = snake.y[j] - bugs[i].y;
                        bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? RIGHT : LEFT) : ((dy > 0) ? DOWN : UP);
                        can_attack_head = true; force_move = true; break;
                    }
                }
                if (!can_attack_head) {
                    bool avoid_segment = false;
                    for (int j = 1; j < snake.length; j++) {
                        if (abs(snake.x[j] - bugs[i].x) <= 2 && abs(snake.y[j] - bugs[i].y) <= 2) {
                            dx = bugs[i].x - snake.x[j]; dy = bugs[i].y - snake.y[j];
                            bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? RIGHT : LEFT) : ((dy > 0) ? DOWN : UP);
                            avoid_segment = true; break;
                        }
                    }
                    if (!avoid_segment) {
                        dx = snake.x[0] - bugs[i].x; dy = snake.y[0] - bugs[i].y;
                        bugs[i].dir = (abs(dx) > abs(dy)) ? ((dx > 0) ? RIGHT : LEFT) : ((dy > 0) ? DOWN : UP);
                    }
                }
                break;
            }
            case RAT_COUNT: break;
        }

        if (!force_move && slow_bugs_active && (rand() % 2 == 0)) continue;

        switch (bugs[i].dir) {
            case UP:    new_y--; break;
            case DOWN:  new_y++; break;
            case LEFT:  new_x--; break;
            case RIGHT: new_x++; break;
        }

        bool valid = true;
        if (new_x <= 0 || new_x >= WIDTH || new_y <= 0 || new_y >= HEIGHT) valid = false;
        
        bool wall_collision = false;
        for (int j = 0; j < wall_count && valid; j++) {
            if (walls[j].x == new_x && walls[j].y == new_y) {
                if (bugs[i].mode == RAT_AGGRESSIVE) destroy_wall(new_x, new_y);
                else { valid = false; wall_collision = true; }
                break;
            }
        }

        if (valid) {
            clear_segment(bugs[i].x, bugs[i].y);
            bugs[i].x = new_x;
            bugs[i].y = new_y;
        } else if (wall_collision) {
            bugs[i].dir = (Direction)(rand() % 4);
            bugs[i].mode_timer = -10;
        }
        bugs[i].previous_mode = previous_mode;
    }
}

// === POWER-UPS AND COLITIONS ===
static void check_eat_fruits() {
    for (int i = 0; i < fruit_count; i++) {
        if (fruits[i].active && snake.x[0] == fruits[i].x && snake.y[0] == fruits[i].y) {
            clear_segment(fruits[i].x, fruits[i].y);
            fruits[i].active = false;
            pending_growth++;
            generate_fruit_at(i);
            printf("\a"); fflush(stdout);
            fruits_eaten++;
            game_stats.fruits_eaten++;
            if (fruits_eaten % 5 == 0 && fruits_eaten > 0) generate_wall();
            if (fruits_eaten % 10 == 0) {
                spawn_bug();
                for (int j = 0; j < bugs_killed_by_body; j++) spawn_bug();
                bugs_killed_by_body = 0;
            }
            if (rand() % 5 == 0) spawn_powerup();
        }
    }
}

static void check_powerup_collision() {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active && snake.x[0] == powerups[i].x && snake.y[0] == powerups[i].y) {
            powerups[i].active = false;
            active_powerups--;
            game_stats.powerups_collected++;
            switch (powerups[i].type) {
                case INVULNERABILITY: is_invulnerable = true; invulnerability_timer = 150; break;
                case SPEED_BOOST: speed_boost_active = true; speed_boost_timer = 100; break;
                case SLOW_BUGS: slow_bugs_active = true; slow_bugs_timer = 120; break;
                case AREA_EXPLOSION:
                    for (int j = 0; j < MAX_BUGS; j++) {
                        if (bugs[j].active && abs(bugs[j].x - snake.x[0]) <= 3 && abs(bugs[j].y - snake.y[0]) <= 3) {
                            clear_segment(bugs[j].x, bugs[j].y);
                            bugs[j].active = false;
                            active_bugs--;
                            game_stats.bugs_killed++;
                            game_stats.bugs_by_type[bugs[j].mode]++;
                        }
                    }
                    break;
            }
            printf("\a"); fflush(stdout);
            break;
        }
    }
}

static void update_powerups() {
    time_t current_time = time(NULL);
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active && (current_time - powerups[i].spawn_time > 20)) {
            clear_segment(powerups[i].x, powerups[i].y);
            powerups[i].active = false;
            active_powerups--;
            if (rand() % 2 == 0) spawn_powerup();
        }
    }

    if (is_invulnerable && --invulnerability_timer <= 0) is_invulnerable = false;
    if (speed_boost_active && --speed_boost_timer <= 0) speed_boost_active = false;
    if (slow_bugs_active && --slow_bugs_timer <= 0) slow_bugs_active = false;
}

static bool check_collision() {
    bool deadly_collision = false;
    bool body_killed_bug = false;

    if (snake.x[0] <= 0 || snake.x[0] >= WIDTH || snake.y[0] <= 0 || snake.y[0] >= HEIGHT) {
        if (!is_invulnerable) deadly_collision = true;
    }

    for (int i = 0; i < wall_count && !deadly_collision; i++) {
        if (snake.x[0] == walls[i].x && snake.y[0] == walls[i].y) {
            if (!is_invulnerable) deadly_collision = true;
            break;
        }
    }

    for (int i = 1; i < snake.length && !deadly_collision; i++) {
        if (snake.x[0] == snake.x[i] && snake.y[0] == snake.y[i]) {
            if (!is_invulnerable) deadly_collision = true;
            break;
        }
    }

    for (int i = 0; i < MAX_BUGS && !deadly_collision; i++) {
        if (!bugs[i].active) continue;
        for (int j = 0; j < snake.length; j++) {
            if (snake.x[j] == bugs[i].x && snake.y[j] == bugs[i].y) {
                if (j < 2) {
                    if (!is_invulnerable) deadly_collision = true;
                } else {
                    clear_segment(bugs[i].x, bugs[i].y);
                    bugs[i].active = false;
                    active_bugs--;
                    bugs_killed_by_body++;
                    game_stats.bugs_killed++;
                    game_stats.bugs_by_type[bugs[i].mode]++;
                    body_killed_bug = true;
                }
                break;
            }
        }
    }

    if (is_invulnerable && (deadly_collision || body_killed_bug)) {
        set_snake_color();
        draw_segment(snake.x[0], snake.y[0], get_head_char());
        reset_color();
    }

    return deadly_collision;
}

// === STATS AND GUI ===
static void init_stats() {
    game_stats.total_time = 0;
    game_stats.fruits_eaten = 0;
    game_stats.max_length = 5;
    game_stats.bugs_killed = 0;
    game_stats.direction_changes = 0;
    game_stats.powerups_collected = 0;
    for (int i = 0; i < RAT_COUNT; i++) game_stats.bugs_by_type[i] = 0;
}

static void update_stats() {
    game_stats.total_time = (int)(time(NULL) - start_time);
    game_stats.max_length = MAX(game_stats.max_length, snake.length);
    if (last_direction != -1 && last_direction != snake.dir) {
        game_stats.direction_changes++;
    }
    last_direction = snake.dir;
}

static void write_stat_line(int line_num, const char* label, const char* value, bool is_substat) {
    const int VALUE_COL = 30;
    const int LINE_DELAY_MS = 250;
    
    printf("\x1b[%d;3H", 5 + line_num);
    
    char display_label[100];
    sprintf(display_label, "%s%s", is_substat ? "  " : "", label);
    
    for (size_t i = 0; i < strlen(display_label); i++) {
        putchar(display_label[i]);
        fflush(stdout);
        usleep(30 * 1000);
    }
    
    int current_pos = 3 + strlen(display_label);
    int dots_needed = VALUE_COL - current_pos;
    if (dots_needed > 0) {
        for(int i = 0; i < dots_needed; i++) putchar('.');
        fflush(stdout);
    }
    
    printf("\x1b[%d;%dH%s ", 5 + line_num, VALUE_COL, value);
    fflush(stdout);
    usleep(LINE_DELAY_MS * 1000);
}

static void show_stats() {
    struct termios original_termios;
    tcgetattr(STDIN_FILENO, &original_termios);
    struct termios raw = original_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    const int BOX_WIDTH = 40;
    int basic_stats = 7;
    int bug_types = 0;
    for (int i = 0; i < RAT_COUNT; i++) {
        if (game_stats.bugs_by_type[i] > 0) bug_types++;
    }
    int total_content_lines = basic_stats + bug_types;
    
    printf("\x1b[2J\x1b[H");
    
    printf("╔"); for(int i=0; i<BOX_WIDTH-2; ++i) printf("="); printf("╗\n");
    printf("║%*s║\n", BOX_WIDTH - 2, "");
    
    const char* title = "FINAL STATISTICS";
    int title_padding = (BOX_WIDTH - 2 - strlen(title)) / 2;
    printf("║%*s%s%*s║\n", title_padding, "", title, BOX_WIDTH - 2 - title_padding - (int)strlen(title), "");
    printf("║%*s║\n", BOX_WIDTH - 2, "");
    
    for (int i = 0; i < total_content_lines; i++) {
        printf("║%*s║\n", BOX_WIDTH - 2, "");
    }
    printf("║%*s║\n", BOX_WIDTH - 2, "");
    printf("╚"); for(int i=0; i<BOX_WIDTH-2; ++i) printf("="); printf("╝\n");

    int current_line = 0;
    char value_buffer[50];
    
    sprintf(value_buffer, "%ds", game_stats.total_time);
    write_stat_line(current_line++, "Total time", value_buffer, false);
    
    sprintf(value_buffer, "%d", game_stats.fruits_eaten);
    write_stat_line(current_line++, "Fruits eaten", value_buffer, false);
    
    sprintf(value_buffer, "%d", game_stats.max_length);
    write_stat_line(current_line++, "Max length", value_buffer, false);

    sprintf(value_buffer, "%d", game_stats.bugs_killed);
    write_stat_line(current_line++, "Bugs eliminated", value_buffer, false);

    for (int i = 0; i < RAT_COUNT; i++) {
        if (game_stats.bugs_by_type[i] > 0) {
            const char* label;
            switch (i) {
                case RAT_ERRATIC:     label = "Erratic"; break;
                case RAT_STALKER:     label = "Stalker"; break;
                case RAT_AMBUSHER:    label = "Ambusher"; break;
                case RAT_PANIC:       label = "Panicked"; break;
                case RAT_PACK_HUNTER: label = "Pack Hunter"; break;
                case RAT_TRAP_MASTER: label = "Trap Master"; break;
                case RAT_U_DODGER:    label = "U-Dodger"; break;
                case RAT_AGGRESSIVE:  label = "Aggressive"; break;
                default:              label = "Unknown"; break;
            }
            sprintf(value_buffer, "%d", game_stats.bugs_by_type[i]);
            write_stat_line(current_line++, label, value_buffer, true);
        }
    }
    
    sprintf(value_buffer, "%d", game_stats.direction_changes);
    write_stat_line(current_line++, "Direction changes", value_buffer, false);
    
    sprintf(value_buffer, "%d", game_stats.powerups_collected);
    write_stat_line(current_line++, "Power-ups collected", value_buffer, false);

    sprintf(value_buffer, "%d", active_bugs);
    write_stat_line(current_line++, "Bugs remaining", value_buffer, false);
    
    printf("\x1b[%d;0H", 8 + total_content_lines);
    fflush(stdout);

    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
}

static void toggle_debug() {
    debug_mode = !debug_mode;
    if (!debug_mode) {
        move_cursor(0, HEIGHT + 2); printf("\x1b[K");
        move_cursor(0, HEIGHT + 3); printf("\x1b[K");
    }
}

static void draw_debug_info() {
    if (!debug_mode) return;
    move_cursor(0, HEIGHT + 2);
    printf("DEBUG: Head(%d,%d) Dir: ", snake.x[0], snake.y[0]);
    switch (snake.dir) {
        case UP: printf("UP"); break; case DOWN: printf("DOWN"); break;
        case LEFT: printf("LEFT"); break; case RIGHT: printf("RIGHT"); break;
    }
    printf(" Invul: %d ", is_invulnerable);
    
    move_cursor(0, HEIGHT + 3);
    printf("Bugs: ");
    for (int i = 0; i < MAX_BUGS; i++) {
        if (bugs[i].active) {
            printf("%d:(%d,%d,", i, bugs[i].x, bugs[i].y);
            switch (bugs[i].mode) {
                case RAT_ERRATIC: printf("ERR"); break; case RAT_STALKER: printf("STK"); break;
                case RAT_AMBUSHER: printf("AMB"); break; case RAT_PANIC: printf("PAN"); break;
                case RAT_PACK_HUNTER: printf("PKH"); break; case RAT_TRAP_MASTER: printf("TRP"); break;
                case RAT_U_DODGER: printf("UDG"); break; case RAT_AGGRESSIVE: printf("AGR"); break;
                default: printf("???"); break;
            }
            printf(") ");
        }
    }
    printf("\x1b[K");
    fflush(stdout);
}

static void draw_logo() {
    printf("\x1b[1;1H");
    set_snake_color();
    printf(
      "\n"
      "      _                   .-=-.         .-==-.         \n"
      "     { }    .-=-.       .' O o '.      /  -<' )--_-<    \n"
      "     { }__ /.' O'.\\ ___/ o .-. O \\___ /  .--v`         \n"
      "      \\ `-` /   \\ O`-'o  /     \\  O`-`o /             \n"
      "       `-.-`     '.____.'       `.____.'               \n"
    );
    reset_color();
}

// === MAIN FUNCTION ===
int main() {
    printf("\x1b[2J\x1b[H");
    fflush(stdout);

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    init_symbols(w.ws_col < 90 ? SYMBOL_SET_ASCII : SYMBOL_SET_UNICODE);
    draw_logo();

    enable_raw_mode();
    get_terminal_size();
    clear_game_area();

    srand(time(NULL));
    start_time = time(NULL);
    init_stats();

    for (int i = 0; i < MAX_BUGS; i++) {
        bugs[i].active = false;
        bugs[i].dir = (Direction)(rand() % 4);
        bugs[i].move_counter = 0;
        bugs[i].mode = (RatMode)(rand() % RAT_COUNT);
        bugs[i].previous_mode = bugs[i].mode;
        bugs[i].mode_timer = 15 + (rand() % 25);
    }
    for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = false;

    snake.length = 5;
    snake.dir = RIGHT;
    snake.x[0] = WIDTH / 2;
    snake.y[0] = HEIGHT / 2;
    for (int i = 1; i < snake.length; i++) {
        snake.x[i] = snake.x[0] - i;
        snake.y[i] = snake.y[0];
    }

    generate_fruits();
    draw_border();
    draw_fruits();
    draw_walls();

    int tick_count = 0;

    while (true) {
        usleep(speed_boost_active ? LOGIC_DELAY_US / 2 : LOGIC_DELAY_US);
        tick_count++;
        
        if (kbhit()) {
            char c = read_key();
            if (c == 'q' || c == 'Q' || c == 27) break;
            if (c == '~') { toggle_debug(); continue; }
            if ((c == 'w' || c == 'W') && snake.dir != DOWN) snake.dir = UP;
            else if ((c == 's' || c == 'S') && snake.dir != UP) snake.dir = DOWN;
            else if ((c == 'a' || c == 'A') && snake.dir != RIGHT) snake.dir = LEFT;
            else if ((c == 'd' || c == 'D') && snake.dir != LEFT) snake.dir = RIGHT;
            else if (c == ' ' || c == '\n') {
                time_t pause_start = time(NULL);
                while (!kbhit()) { usleep(100000); }
                (void)getch();
                start_time += time(NULL) - pause_start;
            }
        }

        if (tick_count >= (speed_boost_active ? FRAME_EVERY_N_TICKS / 2 : FRAME_EVERY_N_TICKS)) {
            tick_count = 0;

            update_stats();
            update_powerups();
            
            check_eat_fruits();
            check_powerup_collision();
            move_snake();
            move_bugs();

            if (check_collision()) break;

            draw_fruits();
            draw_walls();
            draw_bugs();
            draw_powerups();
            draw_debug_info();
            
            int score = snake.length - 5;
            draw_score_time(score, (int)(time(NULL) - start_time));
        }
    }

    disable_raw_mode();
    update_stats();
    show_stats();
    
    return 0;
}