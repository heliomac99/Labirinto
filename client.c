// client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>

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

// Funções auxiliares
void send_action(int sockfd, struct action *act);
void receive_action(int sockfd, struct action *act);
void serialize_action(struct action *act);
void deserialize_action(struct action *act);
void handle_move(int sockfd, struct action *act);
void handle_map(int sockfd, struct action *act);
void print_board(struct action *act);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <endereço IP do servidor> <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in server_addr;

    // Criação do socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Erro ao criar o socket");
        exit(EXIT_FAILURE);
    }

    // Configuração do endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &(server_addr.sin_addr));
    memset(&(server_addr.sin_zero), '\0', 8);

    // Conectar ao servidor
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro ao conectar ao servidor");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Conectado ao servidor.\n");

    char input[BUFFER_SIZE];

    while (1) {
        printf("Digite um comando (start, move, map, hint, reset, exit): ");
        scanf("%s", input);

        int command = -1;
        if (strcasecmp(input, "start") == 0) {
            command = START;
        } else if (strcasecmp(input, "move") == 0) {
            command = MOVE;
        } else if (strcasecmp(input, "map") == 0) {
            command = MAP;
        } else if (strcasecmp(input, "hint") == 0) {
            command = HINT;
        } else if (strcasecmp(input, "reset") == 0) {
            command = RESET;
        } else if (strcasecmp(input, "exit") == 0) {
            command = EXIT;
        } else {
            printf("Comando inválido.\n");
            continue;
        }

        struct action act;
        act.type = command;
        memset(act.moves, 0, sizeof(act.moves));
        memset(act.board, 0, sizeof(act.board));
        send_action(sockfd, &act);

        receive_action(sockfd, &act);

        if (act.type == WIN) {
            printf("You escaped!\n");
            print_board(&act);
            close(sockfd);
            exit(0);
        } else if (act.type == UPDATE) {
            if (command == MOVE) {
                handle_move(sockfd, &act);
            } else if (command == MAP) {
                handle_map(sockfd, &act);
            } else if (command == EXIT) {
                printf("Saindo...\n");
                close(sockfd);
                exit(0);
            } else {
                printf("Servidor confirmou o comando.\n");
            }
        } else {
            printf("Resposta desconhecida do servidor.\n");
        }
    }

    close(sockfd);
    return 0;
}

void send_action(int sockfd, struct action *act) {
    serialize_action(act);
    send(sockfd, act, sizeof(struct action), 0);
}

void receive_action(int sockfd, struct action *act) {
    int num_bytes = recv(sockfd, act, sizeof(struct action), MSG_WAITALL);
    if (num_bytes <= 0) {
        printf("Servidor desconectado.\n");
        exit(1);
    }
    deserialize_action(act);
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

void handle_move(int sockfd, struct action *act) {
    // Exibir movimentos possíveis
    printf("Movimentos possíveis: ");
    for (int i = 0; i < 100; i++) {
        if (act->moves[i] == 0) {
            break;
        }
        switch (act->moves[i]) {
            case 1:
                printf("up ");
                break;
            case 2:
                printf("right ");
                break;
            case 3:
                printf("down ");
                break;
            case 4:
                printf("left ");
                break;
            default:
                break;
        }
    }
    printf("\n");

    // Solicitar direção
    char direction_str[BUFFER_SIZE];
    printf("Digite a direção: ");
    scanf("%s", direction_str);

    int direction = -1;
    if (strcasecmp(direction_str, "up") == 0) {
        direction = 1;
    } else if (strcasecmp(direction_str, "right") == 0) {
        direction = 2;
    } else if (strcasecmp(direction_str, "down") == 0) {
        direction = 3;
    } else if (strcasecmp(direction_str, "left") == 0) {
        direction = 4;
    } else {
        printf("Direção inválida.\n");
        return;
    }

    // Enviar direção escolhida
    act->type = MOVE;
    act->moves[0] = direction;
    memset(&act->moves[1], 0, sizeof(int32_t) * 99);
    memset(act->board, 0, sizeof(act->board));
    send_action(sockfd, act);

    // Receber confirmação
    receive_action(sockfd, act);

    if (act->type == WIN) {
        printf("Parabéns! Você venceu o jogo!\n");
        print_board(act);
        close(sockfd);
        exit(0);
    } else if (act->type == UPDATE) {
        printf("Movimento realizado com sucesso.\n");
    } else {
        printf("Erro ao realizar o movimento.\n");
    }
}

void handle_map(int sockfd, struct action *act) {
    // Exibir o mapa
    print_board(act);
}

void encontradimensoes(int *rows, int *cols, int board[10][10]) {
    int max_rows = 0;
    int max_cols = 0;

    // Encontrar o número real de linhas
    for (int i = 0; i < 10; i++) {
        int is_valid_row = 0;
        for (int j = 0; j < 10; j++) {
            if (board[i][j] != -1) {
                is_valid_row = 1;
                break;
            }
        }
        if (is_valid_row) {
            max_rows++;
        } else {
            break; // Linha com todos -1 encontrada, fim das linhas válidas
        }
    }

    // Encontrar o número real de colunas
    for (int j = 0; j < 10; j++) {
        int is_valid_col = 0;
        for (int i = 0; i < 10; i++) {
            if (board[i][j] != -1) {
                is_valid_col = 1;
                break;
            }
        }
        if (is_valid_col) {
            max_cols++;
        } else {
            break; // Coluna com todos -1 encontrada, fim das colunas válidas
        }
    }

    *rows = max_rows;
    *cols = max_cols;
}

void print_board(struct action *act) {
    int numRows, numCols;
    encontradimensoes(&numRows, &numCols, act->board);

    for (int i = 0; i < numRows; i++) {
        for (int j = 0; j < numCols; j++) {
            int value = act->board[i][j];
            if (value == -1) {
                continue;
            } else if (value == 5) {
                printf("+ ");
            } else if (value == 3) {
                printf("< ");
            } else if (value == 1) {
                printf("_ ");
            } else if (value == 0) {
                printf("# ");
            } else if (value == 2) {
                printf("> "); // Ponto de partida
            } else if (value == 4) {
                printf("? ");
            } else {
                printf("%d ", value);
            }
        }
        printf("\n");
    }
}
