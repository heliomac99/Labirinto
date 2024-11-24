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

// Definição dos comandos
enum Commands { START = 0, MOVE = 1, MAP = 2, HINT = 3, UPDATE = 4, WIN = 5 , RESET = 6, EXIT = 7, ERROR = 8 };

// Definição da estrutura Action
#pragma pack(1)
struct action {
    int32_t type;
    int32_t moves[100];
    int32_t board[10][10];
    char error_message[256];
};
#pragma pack()

// Definição da estrutura GameState
typedef struct {
    uint32_t actual_rows; // Número real de linhas do mapa
    uint32_t actual_cols; // Número real de colunas do mapa
    uint32_t player_i;
    uint32_t player_j;
    uint32_t inicio_i;
    uint32_t inicio_j;
    uint32_t fim_i;
    uint32_t fim_j;
    uint32_t game_over; // Novo campo para indicar se o jogo acabou
    uint32_t game_inicialized; // Novo campo para indicar se o jogo acabou
    int32_t matrix[MAX_ROWS][MAX_COLS];
    int32_t matrix_decoberto[MAX_ROWS][MAX_COLS];
} GameState;

// Funções auxiliares
void read_matrix_from_file(const char *filename, GameState *gameState);
void initialize_game(GameState *gameState, const char *filename);
void handle_client(int client_fd, GameState *gameState, const char *filename);
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

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <v4|v6> <porta> -i <arquivo de entrada da matriz>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *ip_version = argv[1];
    char *port = argv[2];
    char *input_flag = argv[3];
    char *input_file = argv[4];

    if (strcmp(input_flag, "-i") != 0) {
        fprintf(stderr, "Uso: %s <v4|v6> <porta> -i <arquivo de entrada da matriz>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int server_fd;
    int opt = 1;
    struct addrinfo hints, *res, *p;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // Use o endereço IP do sistema

    if (strcmp(ip_version, "v4") == 0) {
        hints.ai_family = AF_INET;
    } else if (strcmp(ip_version, "v6") == 0) {
        hints.ai_family = AF_INET6;
    } else {
        fprintf(stderr, "Versão IP inválida: %s. Use v4 ou v6.\n", ip_version);
        exit(EXIT_FAILURE);
    }

    if ((status = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "Erro em getaddrinfo: %s\n", gai_strerror(status));
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

        break; // Sucesso
    }

    if (p == NULL) {
        fprintf(stderr, "server: falha ao bindar\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    if (listen(server_fd, 1) == -1) {
        perror("Erro no listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    //printf("Servidor esperando por conexões na porta %s...\n", port);

    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Aceitar conexão
    int client_fd;
    if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
        perror("Erro no accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("client connected.\n");

    // Inicializar o jogo
    GameState gameState;
    init_game_state(&gameState);
    // Lidar com o cliente
    handle_client(client_fd, &gameState, input_file);

    // Fechar sockets
    close(client_fd);
    close(server_fd);

    return 0;
}

void read_matrix_from_file(const char *filename, GameState *gameState) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Erro ao abrir o arquivo");
        exit(EXIT_FAILURE);
    }

    // Inicializar a matriz com -1
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
                value = 5; // Representar o jogador com 5
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
            fprintf(stderr, "Erro: Número inconsistente de colunas na linha %d.\n", row + 1);
            fclose(file);
            exit(EXIT_FAILURE);
        }

        row++;
    }

    gameState->actual_rows = row;              // Número real de linhas do mapa
    gameState->actual_cols = cols_in_first_row; // Número real de colunas do mapa

    fclose(file);
}

void initialize_game(GameState *gameState, const char *filename) {
    memset(gameState, 0, sizeof(GameState));
    read_matrix_from_file(filename, gameState);
    set_matrix_descoberto_to_zeros(gameState);
    mark_positions_around_player(gameState);
    gameState->game_over = 0; // Inicializar o jogo como não terminado
    gameState->game_inicialized = 1; // Setar o jogo como iniciado
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
            int chebyshev_distance = (delta_i > delta_j) ? delta_i : delta_j; // Distância de Chebyshev

            if (chebyshev_distance <= 1) {
                gameState->matrix_decoberto[i][j] = 1; // Marca a posição como 1
            }
        }
    }
}

void handle_client(int client_fd, GameState *gameState, const char *filename) {
    struct action act;
    int num_bytes;

    while ((num_bytes = recv(client_fd, &act, sizeof(struct action), MSG_WAITALL)) > 0) {
        deserialize_action(&act);
        process_action(client_fd, &act, gameState, filename);
    }

    if (num_bytes == -1) {
        perror("Erro no recv");
    }
}

void reset_game(GameState *gameState) {

    gameState->matrix[gameState->player_i][gameState->player_j] = 1; // Marcar como caminho livre ou como estava antes
    gameState->matrix[gameState->fim_i][gameState->fim_j] = 3; // Marcar como saída

    // Restaurar a posição inicial do jogador
    gameState->player_i = gameState->inicio_i;
    gameState->player_j = gameState->inicio_j;

    // Atualizar a matriz com a nova posição do jogador
    gameState->matrix[gameState->player_i][gameState->player_j] = 5; // Representar o jogador

    // Redefinir o estado do jogo se necessário
    gameState->game_over = 0;
}

void process_action(int client_fd, struct action *act, GameState *gameState, const char *filename) {
    
    if (act->type != START && gameState->game_inicialized == 0) {
        build_error(act, "error: start the game first");
        send_action(client_fd, act);
    } else {
        switch (act->type) {
            case START:
                if(!gameState->game_over) {
                    initialize_game(gameState, filename);
                    memset(act->moves, 0, sizeof(act->moves));
                    memset(act->board, 0, sizeof(act->board));
                    fill_possible_moves(gameState, act);
                    act->type = UPDATE;
                    send_action(client_fd, act);
                }
                break;

            case MOVE: {
                if(!gameState->game_over){
                    // Pega direção inserida pelo cliente no moves e executa o movimento
                    int direction = act->moves[0];
                    if(move_player(gameState, direction) == 1) {
                        if (gameState->game_over) {
                            // Enviar mensagem de vitória com tipo WIN
                            act->type = WIN;
                            memset(act->moves, 0, sizeof(act->moves));
                            memset(act->board, 0, sizeof(act->board));
                            copy_board_to_action(gameState, act);
                            act->board[gameState->fim_i][gameState->fim_j] = 3;
                            send_action(client_fd, act);
                        } else {
                            // Enviar atualização normal com tipo UPDATE
                            act->type = UPDATE;
                            memset(act->moves, 0, sizeof(act->moves));
                            memset(act->board, 0, sizeof(act->board));
                            fill_possible_moves(gameState, act);
                            send_action(client_fd, act);
                        }
                    }
                    else{
                        build_error(act, "error: you cannot go this way");
                        send_action(client_fd, act);
                    }
                }
                break;
            }

            case MAP:
                if(!gameState->game_over){
                    // Preparar a ação com o mapa parcial
                    copy_board_to_action(gameState, act);

                    // Marcar posições fora do alcance com 4
                    fill_unreachable_positions(gameState, act);

                    // Enviar o mapa parcial com tipo UPDATE
                    act->type = UPDATE;
                    memset(act->moves, 0, sizeof(act->moves));
                    send_action(client_fd, act);
                } 
        
                break;

            case RESET:
                // Chamar a função para reiniciar o jogo
                initialize_game(gameState, filename);

                // Enviar confirmação com tipo UPDATE
                act->type = UPDATE;
                memset(act->moves, 0, sizeof(act->moves));
                memset(act->board, 0, sizeof(act->board));
                fill_possible_moves(gameState, act);
                send_action(client_fd, act);
                break;

            case EXIT:
                printf("client disconnected\n");

                act->type = UPDATE;
                memset(act->moves, 0, sizeof(act->moves));
                memset(act->board, 0, sizeof(act->board));
                send_action(client_fd, act);

                close(client_fd);
                exit(0); 
                break;

            default:
                // Enviar mensagem de erro com tipo ERROR
                build_error(act, "error: command not found");
                send_action(client_fd, act);
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

        // Atualizar a posição anterior
        if (gameState->player_i == gameState->inicio_i && gameState->player_j == gameState->inicio_j)
            gameState->matrix[gameState->player_i][gameState->player_j] = 2; // Posição inicial
        else
            gameState->matrix[gameState->player_i][gameState->player_j] = 1; // Caminho livre

        gameState->player_i = new_i;
        gameState->player_j = new_j;

        mark_positions_around_player(gameState);

        if (cell_value == 3) {
            // O jogador alcançou a saída
            gameState->game_over = 1;
            gameState->matrix[new_i][new_j] = 5; // Atualizar para representar o jogador
        } else {
            gameState->matrix[new_i][new_j] = 5; // Atualizar para representar o jogador
        }
        

        return 1; // Significa que o jogador caminhou
    } else {
        return 0; // Significa que o jogador não caminhou
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
    // Copiar o estado atual da matriz para a ação
    for (int i = 0; i < MAX_ROWS; i++) {
        for (int j = 0; j < MAX_COLS; j++) {
            act->board[i][j] = gameState->matrix[i][j];
        }
    }
}

void fill_unreachable_positions(GameState *gameState, struct action *act) {
    int player_i = gameState->player_i;
    int player_j = gameState->player_j;

    // Definir o raio de visibilidade (uma casa de alcance, incluindo diagonais)
    int visibility_radius = 1;

    for (uint32_t i = 0; i < gameState->actual_rows; i++) {
        for (uint32_t j = 0; j < gameState->actual_cols; j++) {
            int delta_i = abs((int)i - player_i);
            int delta_j = abs((int)j - player_j);
            int distance = delta_i > delta_j ? delta_i : delta_j; // Distância de Chebyshev

            if (distance > visibility_radius && gameState->matrix[i][j] != -1 && gameState->matrix_decoberto[i][j] == 0) {
                act->board[i][j] = 4; // Marcar como não visível
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
    // Calcular movimentos possíveis
    int possible_moves[4];
    calculate_possible_moves(gameState, possible_moves);
    // Preencher o campo moves da ação
    memset(act->moves, 0, sizeof(act->moves));
    int idx = 0;
    for (int i = 0; i < 4; i++) {
        if (possible_moves[i]) {
            act->moves[idx++] = i + 1;
        }
    }
}

void build_error(struct action *act, const char* msg){
    act->type = ERROR;
    strcpy(act->error_message, msg);
    memset(act->moves, 0, sizeof(act->moves));
    memset(act->board, 0, sizeof(act->board));
}
