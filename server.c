// server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>

#define PORT 51511
#define BUFFER_SIZE 1024
#define MAX_ROWS 10
#define MAX_COLS 10

// Definição dos comandos
enum Commands { START = 0, MOVE = 1, MAP = 2, HINT = 3, UPDATE = 4, WIN = 5 , RESET = 6, EXIT = 7 };

// Definição da estrutura Action
#pragma pack(1)
struct action {
    int32_t type;
    int32_t moves[100];
    int32_t board[10][10];
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
    uint32_t game_over; // Novo campo para indicar se o jogo acabou
    int32_t matrix[MAX_ROWS][MAX_COLS];
} GameState;

// Funções auxiliares
void read_matrix_from_file(const char *filename, GameState *gameState);
void initialize_game(GameState *gameState, const char *filename);
void handle_client(int client_fd, GameState *gameState);
void process_action(int client_fd, struct action *act, GameState *gameState);
void send_action(int client_fd, struct action *act);
void serialize_action(struct action *act);
void deserialize_action(struct action *act);
void move_player(GameState *gameState, int direction);
void calculate_possible_moves(GameState *gameState, int possible_moves[4]);
void copy_board_to_action(GameState *gameState, struct action *act);
void fill_unreachable_positions(GameState *gameState, struct action *act);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <arquivo de entrada da matriz>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int server_fd, client_fd;
    int opt = 1;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Criação do socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Erro ao criar o socket");
        exit(EXIT_FAILURE);
    }

    // Permitir reutilizar o endereço
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Erro ao definir SO_REUSEADDR");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Configuração do endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), '\0', 8);

    // Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro no bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 1) == -1) {
        perror("Erro no listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor esperando por conexões na porta %d...\n", PORT);

    // Aceitar conexão
    if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
        perror("Erro no accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Cliente conectado.\n");

    // Inicializar o jogo
    GameState gameState;
    initialize_game(&gameState, argv[1]);

    // Lidar com o cliente
    handle_client(client_fd, &gameState);

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
                // Representa a saída
                // Não precisa fazer nada especial aqui, apenas manter o valor
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
    gameState->game_over = 0; // Inicializar o jogo como não terminado
}

void handle_client(int client_fd, GameState *gameState) {
    struct action act;
    int num_bytes;

    while ((num_bytes = recv(client_fd, &act, sizeof(struct action), MSG_WAITALL)) > 0) {
        deserialize_action(&act);
        process_action(client_fd, &act, gameState);
    }

    if (num_bytes == -1) {
        perror("Erro no recv");
    } else if (num_bytes == 0) {
        printf("Cliente desconectado.\n");
    }
}

void reset_game(GameState *gameState) {

    gameState->matrix[gameState->player_i][gameState->player_j] = 1; // Marcar como caminho livre ou como estava antes

    // Restaurar a posição inicial do jogador
    gameState->player_i = gameState->inicio_i;
    gameState->player_j = gameState->inicio_j;

    // Atualizar a matriz com a nova posição do jogador
    gameState->matrix[gameState->player_i][gameState->player_j] = 5; // Representar o jogador

    // Redefinir o estado do jogo se necessário
    gameState->game_over = 0;
}


void process_action(int client_fd, struct action *act, GameState *gameState) {
    switch (act->type) {
        case START:
            printf("Cliente iniciou o jogo.\n");
            // Enviar confirmação com tipo UPDATE
            act->type = UPDATE;
            memset(act->moves, 0, sizeof(act->moves));
            memset(act->board, 0, sizeof(act->board));
            send_action(client_fd, act);
            break;

        case MOVE: {
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

            // Enviar os movimentos possíveis com tipo UPDATE
            act->type = UPDATE;
            memset(act->board, 0, sizeof(act->board));
            send_action(client_fd, act);

            // Receber a ação com a direção escolhida
            int num_bytes = recv(client_fd, act, sizeof(struct action), MSG_WAITALL);
            if (num_bytes <= 0) {
                printf("Erro ao receber a direção.\n");
                return;
            }
            deserialize_action(act);

            // Executar o movimento
            int direction = act->moves[0];
            move_player(gameState, direction);

            if (gameState->game_over) {
                // Enviar mensagem de vitória com tipo WIN
                act->type = WIN;
                memset(act->moves, 0, sizeof(act->moves));
                memset(act->board, 0, sizeof(act->board));
                copy_board_to_action(gameState, act);
                send_action(client_fd, act);
                // Fechar conexão ou reiniciar o jogo
                close(client_fd);
                exit(0);
            } else {
                // Enviar atualização normal com tipo UPDATE
                act->type = UPDATE;
                memset(act->moves, 0, sizeof(act->moves));
                memset(act->board, 0, sizeof(act->board));
                send_action(client_fd, act);
            }
            break;
        }

        case MAP: {
            // Preparar a ação com o mapa parcial
            copy_board_to_action(gameState, act);

            // Marcar posições fora do alcance com 4
            fill_unreachable_positions(gameState, act);

            // Enviar o mapa parcial com tipo UPDATE
            act->type = UPDATE;
            memset(act->moves, 0, sizeof(act->moves));
            send_action(client_fd, act);
            break;
        }

        case RESET:
            // Chamar a função para reiniciar o jogo
            reset_game(gameState);
            // Enviar confirmação com tipo UPDATE
            act->type = UPDATE;
            memset(act->moves, 0, sizeof(act->moves));
            memset(act->board, 0, sizeof(act->board));
            send_action(client_fd, act);
            break;

        case EXIT:
            printf("Cliente solicitou saída.\n");
            // Enviar confirmação com tipo UPDATE
            act->type = UPDATE;
            memset(act->moves, 0, sizeof(act->moves));
            memset(act->board, 0, sizeof(act->board));
            send_action(client_fd, act);
            close(client_fd);
            exit(0);
            break;

            

        default:
            printf("Comando inválido recebido.\n");
            // Enviar mensagem de erro com tipo UPDATE
            act->type = UPDATE;
            memset(act->moves, 0, sizeof(act->moves));
            memset(act->board, 0, sizeof(act->board));
            send_action(client_fd, act);
            break;
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

void move_player(GameState *gameState, int direction) {
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
            printf("Direção inválida.\n");
            return;
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

        if (cell_value == 3) {
            // O jogador alcançou a saída
            gameState->game_over = 1;
            gameState->matrix[new_i][new_j] = 5; // Atualizar para representar o jogador
        } else {
            gameState->matrix[new_i][new_j] = 5; // Atualizar para representar o jogador
        }
    } else {
        printf("Movimento inválido para a posição (%d, %d).\n", new_i, new_j);
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

            if (distance > visibility_radius && gameState->matrix[i][j] != -1) {
                    act->board[i][j] = 4; // Marcar como não visível
            }
        }
    }
}
