// server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>

#define BUFFER_SIZE 1024
#define MAX_ROWS 10
#define MAX_COLS 10

// Definition of commands
enum Commands { START = 0, MOVE = 1, MAP = 2, HINT = 3, UPDATE = 4, WIN = 5 , RESET = 6, EXIT = 7, ERROR = 8, GAMEOVER = 9 };

// Definition of the action structure
#pragma pack(1)
struct action {
    int32_t type;
    int32_t moves[100];
    int32_t board[10][10];
    char error_message[256];
};
#pragma pack()

// Definition of the GameState structure
typedef struct {
    uint32_t actual_rows; // Actual number of rows in the map
    uint32_t actual_cols; // Actual number of columns in the map
    uint32_t player_i;
    uint32_t player_j;
    uint32_t inicio_i;
    uint32_t inicio_j;
    uint32_t fim_i;
    uint32_t fim_j;
    uint32_t game_over; // New field to indicate if the game is over
    uint32_t game_inicialized; // New field to indicate if the game is initialized
    int32_t matrix[MAX_ROWS][MAX_COLS];
    int32_t matrix_decoberto[MAX_ROWS][MAX_COLS];
} GameState;

// Function prototypes
void read_matrix_from_file(const char *filename, GameState *gameState);
void initialize_game(GameState *gameState, const char *filename);
int handle_client(int client_fd, GameState *gameState, const char *filename);
void process_action(int client_fd, struct action *act, GameState *gameState, const char *filename);
void send_action(int client_fd, struct action *act);
void serialize_action(struct action *act);
void deserialize_action(struct action *act);
int move_player(GameState *gameState, int direction);
void calculate_possible_moves(GameState *gameState, int possible_moves[4]);
void copy_board_to_action(GameState *gameState, struct action *act);
void fill_unreachable_positions(GameState *gameState, struct action *act);
void fill_possible_moves(GameState *gameState, struct action *act);
void init_game_state(GameState *game_state);
void build_error(struct action *act, const char* msg);
void reset_game(GameState *gameState);
void set_matrix_descoberto_to_zeros(GameState *gameState);
void mark_positions_around_player(GameState *gameState);

// Handler function prototypes
void handle_start(int client_fd, struct action *act, GameState *gameState, const char *filename);
void handle_move(int client_fd, struct action *act, GameState *gameState);
void handle_map(int client_fd, struct action *act, GameState *gameState);
void handle_reset(int client_fd, struct action *act, GameState *gameState, const char *filename);
void handle_exit(int client_fd, struct action *act);
void handle_default(int client_fd, struct action *act);
void handle_game_not_inicialized(int client_fd, struct action *act);
void handle_game_over(int client_fd, struct action *act, GameState *gameState, const char *filename);
void handle_commands_game_over(int client_fd, struct action *act);

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <v4|v6> <port> -i <input matrix file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *ip_version = argv[1];
    char *port = argv[2];
    char *input_flag = argv[3];
    char *input_file = argv[4];

    if (strcmp(input_flag, "-i") != 0) {
        fprintf(stderr, "Usage: %s <v4|v6> <port> -i <input matrix file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int server_fd;
    int opt = 1;
    struct addrinfo hints, *res, *p;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // Use the system's IP address

    if (strcmp(ip_version, "v4") == 0) {
        hints.ai_family = AF_INET;
    } else if (strcmp(ip_version, "v6") == 0) {
        hints.ai_family = AF_INET6;
    } else {
        fprintf(stderr, "Invalid IP version: %s. Use v4 or v6.\n", ip_version);
        exit(EXIT_FAILURE);
    }

    if ((status = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "Error in getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            perror("setsockopt");
            close(server_fd);
            continue;
        }

        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_fd);
            perror("server: bind");
            continue;
        }

        break; // Success
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    if (listen(server_fd, 1) == -1) {
        perror("Error in listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        // Accept connection
        int client_fd;
        if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
            perror("Error in accept");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        printf("client connected.\n");

        // Initialize the game
        GameState gameState;
        init_game_state(&gameState);
        // Handle the client

        int bytes = handle_client(client_fd, &gameState, input_file);
        if (bytes == -1 || bytes == 0) {
            close(client_fd);
        }
    }

    close(server_fd);
    return 0;
}

void read_matrix_from_file(const char *filename, GameState *gameState) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening the file");
        exit(EXIT_FAILURE);
    }

    // Initialize the matrix with -1
    for (int i = 0; i < MAX_ROWS; i++) {
        for (int j = 0; j < MAX_COLS; j++) {
            gameState->matrix[i][j] = -1;
        }
    }

    char line[BUFFER_SIZE];
    int row = 0;
    int cols_in_first_row = -1;

    while (fgets(line, sizeof(line), file) && row < MAX_ROWS) {
        int col = 0;
        char *token = strtok(line, " \t\n");
        while (token && col < MAX_COLS) {
            int value = atoi(token);
            if (value == 2) {
                value = 5; // Represent the player with 5
                gameState->player_i = row;
                gameState->player_j = col;
                gameState->inicio_i = row;
                gameState->inicio_j = col;
            } else if (value == 3) {
                gameState->fim_i = row;
                gameState->fim_j = col;
            }
            gameState->matrix[row][col] = value;
            col++;
            token = strtok(NULL, " \t\n");
        }

        if (cols_in_first_row == -1) {
            cols_in_first_row = col;
        } else if (col != cols_in_first_row) {
            fprintf(stderr, "Error: Inconsistent number of columns in line %d.\n", row + 1);
            fclose(file);
            exit(EXIT_FAILURE);
        }

        row++;
    }

    gameState->actual_rows = row;              // Actual number of rows in the map
    gameState->actual_cols = cols_in_first_row; // Actual number of columns in the map

    fclose(file);
}

void initialize_game(GameState *gameState, const char *filename) {
    memset(gameState, 0, sizeof(GameState));
    read_matrix_from_file(filename, gameState);
    set_matrix_descoberto_to_zeros(gameState);
    mark_positions_around_player(gameState);
    gameState->game_over = 0; // Initialize the game as not over
    gameState->game_inicialized = 1; // Set the game as initialized
    printf("starting new game\n");
}

void set_matrix_descoberto_to_zeros(GameState *gameState) {
    for (uint32_t i = 0; i < gameState->actual_rows; i++) {
        for (uint32_t j = 0; j < gameState->actual_cols; j++) {
            gameState->matrix_decoberto[i][j] = 0;
        }
    }
}

void mark_positions_around_player(GameState *gameState) {
    for (uint32_t i = 0; i < gameState->actual_rows; i++) {
        for (uint32_t j = 0; j < gameState->actual_cols; j++) {
            int delta_i = i - gameState->player_i;
            int delta_j = j - gameState->player_j;
            int chebyshev_distance = (delta_i > delta_j) ? delta_i : delta_j; // Chebyshev distance

            if (chebyshev_distance <= 1) {
                gameState->matrix_decoberto[i][j] = 1; // Mark the position as 1
            }
        }
    }
}

int handle_client(int client_fd, GameState *gameState, const char *filename) {
    struct action act;
    int num_bytes;

    while ((num_bytes = recv(client_fd, &act, sizeof(struct action), MSG_WAITALL)) > 0) {
        deserialize_action(&act);
        process_action(client_fd, &act, gameState, filename);
    }

    if (num_bytes == 0) {
        return 0;
    }

    if (num_bytes == -1) {
        return -1;
    }

    return INT32_MIN;
}

void process_action(int client_fd, struct action *act, GameState *gameState, const char *filename) {
    if(gameState->game_over){
        handle_game_over(client_fd, act, gameState, filename);
    } else if (act->type != START && gameState->game_inicialized == 0) {
        handle_game_not_inicialized(client_fd, act);
    } else {
        switch (act->type) {
            case START:
                handle_start(client_fd, act, gameState, filename);
                break;

            case MOVE:
                handle_move(client_fd, act, gameState);
                break;

            case MAP:
                handle_map(client_fd, act, gameState);
                break;

            case RESET:
                handle_reset(client_fd, act, gameState, filename);
                break;

            case EXIT:
                handle_exit(client_fd, act);
                break;

            default:
                handle_default(client_fd, act);
                break;
        }
    }
}

void send_action(int client_fd, struct action *act) {
    serialize_action(act);
    send(client_fd, act, sizeof(struct action), 0);
}

void serialize_action(struct action *act) {
    act->type = htonl(act->type);
    for (int i = 0; i < 100; i++) {
        act->moves[i] = htonl(act->moves[i]);
    }
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            act->board[i][j] = htonl(act->board[i][j]);
        }
    }
}

void deserialize_action(struct action *act) {
    act->type = ntohl(act->type);
    for (int i = 0; i < 100; i++) {
        act->moves[i] = ntohl(act->moves[i]);
    }
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            act->board[i][j] = ntohl(act->board[i][j]);
        }
    }
}

int move_player(GameState *gameState, int direction) {
    int new_i = gameState->player_i;
    int new_j = gameState->player_j;

    switch (direction) {
        case 1: // UP
            new_i--;
            break;
        case 2: // RIGHT
            new_j++;
            break;
        case 3: // DOWN
            new_i++;
            break;
        case 4: // LEFT
            new_j--;
            break;
        default:
            return 0;
    }

    if (new_i >= 0 && new_i < (int)gameState->actual_rows &&
        new_j >= 0 && new_j < (int)gameState->actual_cols &&
        gameState->matrix[new_i][new_j] != 0 && gameState->matrix[new_i][new_j] != -1) {

        int cell_value = gameState->matrix[new_i][new_j];

        // Update the previous position
        if (gameState->player_i == gameState->inicio_i && gameState->player_j == gameState->inicio_j)
            gameState->matrix[gameState->player_i][gameState->player_j] = 2; // Starting position
        else
            gameState->matrix[gameState->player_i][gameState->player_j] = 1; // Free path

        gameState->player_i = new_i;
        gameState->player_j = new_j;

        mark_positions_around_player(gameState);

        if (cell_value == 3) {
            // The player reached the exit
            gameState->game_over = 1;
            gameState->matrix[new_i][new_j] = 5; // Update to represent the player
        } else {
            gameState->matrix[new_i][new_j] = 5; // Update to represent the player
        }

        return 1; // Indicates that the player moved
    } else {
        return 0; // Indicates that the player did not move
    }
}

void calculate_possible_moves(GameState *gameState, int possible_moves[4]) {
    int i = gameState->player_i;
    int j = gameState->player_j;

    possible_moves[0] = (i > 0 && gameState->matrix[i - 1][j] != 0 && gameState->matrix[i - 1][j] != -1) ? 1 : 0; // UP
    possible_moves[1] = (j < (int)gameState->actual_cols - 1 && gameState->matrix[i][j + 1] != 0 && gameState->matrix[i][j + 1] != -1) ? 1 : 0; // RIGHT
    possible_moves[2] = (i < (int)gameState->actual_rows - 1 && gameState->matrix[i + 1][j] != 0 && gameState->matrix[i + 1][j] != -1) ? 1 : 0; // DOWN
    possible_moves[3] = (j > 0 && gameState->matrix[i][j - 1] != 0 && gameState->matrix[i][j - 1] != -1) ? 1 : 0; // LEFT
}

void copy_board_to_action(GameState *gameState, struct action *act) {
    // Copy the current state of the matrix to the action
    for (int i = 0; i < MAX_ROWS; i++) {
        for (int j = 0; j < MAX_COLS; j++) {
            act->board[i][j] = gameState->matrix[i][j];
        }
    }
}

void fill_unreachable_positions(GameState *gameState, struct action *act) {
    int player_i = gameState->player_i;
    int player_j = gameState->player_j;

    // Define the visibility radius (one square range, including diagonals)
    int visibility_radius = 1;

    for (uint32_t i = 0; i < gameState->actual_rows; i++) {
        for (uint32_t j = 0; j < gameState->actual_cols; j++) {
            int delta_i = abs((int)i - player_i);
            int delta_j = abs((int)j - player_j);
            int distance = delta_i > delta_j ? delta_i : delta_j; // Chebyshev distance

            if (distance > visibility_radius && gameState->matrix[i][j] != -1 && gameState->matrix_decoberto[i][j] == 0) {
                act->board[i][j] = 4; // Mark as not visible
            }
        }
    }
}

void init_game_state(GameState *game_state) {
    game_state->actual_rows = 0;
    game_state->actual_cols = 0;
    game_state->player_i = -1;
    game_state->player_j = -1;
    game_state->inicio_i = -1;
    game_state->inicio_j = -1;
    game_state->game_over = 0;
    game_state->game_inicialized = 0;
}

void fill_possible_moves(GameState *gameState, struct action *act) {
    // Calculate possible moves
    int possible_moves[4];
    calculate_possible_moves(gameState, possible_moves);
    // Fill the moves field of the action
    memset(act->moves, 0, sizeof(act->moves));
    int idx = 0;
    for (int i = 0; i < 4; i++) {
        if (possible_moves[i]) {
            act->moves[idx++] = i + 1;
        }
    }
}

void build_error(struct action *act, const char* msg) {
    act->type = ERROR;
    strcpy(act->error_message, msg);
    memset(act->moves, 0, sizeof(act->moves));
    memset(act->board, 0, sizeof(act->board));
}

void handle_start(int client_fd, struct action *act, GameState *gameState, const char *filename) {
    if (!gameState->game_over) {
        initialize_game(gameState, filename);
        memset(act->moves, 0, sizeof(act->moves));
        memset(act->board, 0, sizeof(act->board));
        fill_possible_moves(gameState, act);
        act->type = UPDATE;
        send_action(client_fd, act);
    }
}

void handle_move(int client_fd, struct action *act, GameState *gameState) {
    if (!gameState->game_over) {
        // Get the direction from the client's action and execute the movement
        int direction = act->moves[0];
        if (move_player(gameState, direction) == 1) {
            if (gameState->game_over) {
                // Send victory message with type WIN
                act->type = WIN;
                memset(act->moves, 0, sizeof(act->moves));
                memset(act->board, 0, sizeof(act->board));
                copy_board_to_action(gameState, act);
                act->board[gameState->fim_i][gameState->fim_j] = 3;
                send_action(client_fd, act);
            } else {
                // Send normal update with type UPDATE
                act->type = UPDATE;
                memset(act->moves, 0, sizeof(act->moves));
                memset(act->board, 0, sizeof(act->board));
                fill_possible_moves(gameState, act);
                send_action(client_fd, act);
            }
        } else {
            build_error(act, "error: you cannot go this way");
            send_action(client_fd, act);
        }
    }
}

void handle_map(int client_fd, struct action *act, GameState *gameState) {
    if (!gameState->game_over) {
        // Prepare the action with the partial map
        copy_board_to_action(gameState, act);

        // Mark positions out of reach with 4
        fill_unreachable_positions(gameState, act);

        // Send the partial map with type UPDATE
        act->type = UPDATE;
        memset(act->moves, 0, sizeof(act->moves));
        send_action(client_fd, act);
    }
}

void handle_reset(int client_fd, struct action *act, GameState *gameState, const char *filename) {
    // Call the function to restart the game
    initialize_game(gameState, filename);

    // Send confirmation with type UPDATE
    act->type = UPDATE;
    memset(act->moves, 0, sizeof(act->moves));
    memset(act->board, 0, sizeof(act->board));
    fill_possible_moves(gameState, act);
    send_action(client_fd, act);
}

void handle_exit(int client_fd, struct action *act) {
    printf("client disconnected\n");

    act->type = UPDATE;
    memset(act->moves, 0, sizeof(act->moves));
    memset(act->board, 0, sizeof(act->board));
    send_action(client_fd, act);

    close(client_fd);
}

void handle_default(int client_fd, struct action *act) {
    // Send error message with type ERROR
    build_error(act, "error: command not found");
    send_action(client_fd, act);
}

void handle_game_not_inicialized(int client_fd, struct action *act) {
    // Send error message with type ERROR
    build_error(act, "error: start the game first");
    send_action(client_fd, act);
}

void handle_commands_game_over(int client_fd, struct action *act) {
    act->type = GAMEOVER;
    memset(act->moves, 0, sizeof(act->moves));
    memset(act->board, 0, sizeof(act->board));
    send_action(client_fd, act);
}

void handle_game_over(int client_fd, struct action *act, GameState *gameState, const char *filename) {
    switch (act->type) {
            case START:
                handle_commands_game_over(client_fd, act);
                break;

            case MOVE:
                handle_commands_game_over(client_fd, act);
                break;

            case MAP:
                handle_commands_game_over(client_fd, act);
                break;

            case RESET:
                handle_reset(client_fd, act, gameState, filename);
                break;

            case EXIT:
                handle_exit(client_fd, act);
                break;

            default:
                handle_default(client_fd, act);
                break;
        }
}
