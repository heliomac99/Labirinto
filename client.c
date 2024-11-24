// client.c

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
enum Commands { START = 0, MOVE = 1, MAP = 2, HINT = 3, UPDATE = 4, WIN = 5 , RESET = 6, EXIT = 7, ERROR = 8, GAMEOVER = 9 };
enum Directions { UP = 1, RIGHT = 2, DOWN = 3, LEFT = 4};

// Definição da estrutura Action
#pragma pack(1)
struct action {
    int32_t type;
    int32_t moves[100];
    int32_t board[10][10];
    char error_message[256]; // Novo campo para a mensagem de erro
};
#pragma pack()

// Funções auxiliares
void send_action(int sockfd, struct action *act);
void receive_action(int sockfd, struct action *act);
void serialize_action(struct action *act);
void deserialize_action(struct action *act);
void handle_move(struct action *act);
void handle_reset(struct action *act);
void handle_start(struct action *act);
void handle_map(struct action *act);
void handle_error(struct action *act);
void print_board(struct action *act);
void print_possible_moves(struct action* act);
void encontradimensoes(int *rows, int *cols, int board[10][10]);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <endereço IP do servidor> <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct addrinfo hints, *res, *p;
    int status;

    // Configuração de hints para getaddrinfo
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;

    // Resolver o endereço do servidor
    if ((status = getaddrinfo(argv[1], argv[2], &hints, &res)) != 0) {
        fprintf(stderr, "Erro em getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    // Tentar conectar a um dos resultados retornados
    for (p = res; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break; // Sucesso
    }

    if (p == NULL) {
        fprintf(stderr, "client: falha ao conectar\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res); // Não precisamos mais da lista ligada de resultados

    char input[BUFFER_SIZE];

    while (1) {
        scanf("%s", input);

        struct action act;
        memset(act.moves, 0, sizeof(act.moves));
        memset(act.board, 0, sizeof(act.board));

        int command = -1;
        if (strcasecmp(input, "start") == 0) {
            command = START;
        } else if (strcasecmp(input, "map") == 0) {
            command = MAP;
        } else if (strcasecmp(input, "hint") == 0) {
            command = HINT;
        } else if (strcasecmp(input, "reset") == 0) {
            command = RESET;
        } else if (strcasecmp(input, "exit") == 0) {
            command = EXIT;
        } else if (strcasecmp(input, "up") == 0) {
            command = MOVE;
            act.moves[0] = UP;
        } else if (strcasecmp(input, "right") == 0) {
            command = MOVE;
            act.moves[0] = RIGHT;
        } else if (strcasecmp(input, "down") == 0) {
            command = MOVE;
            act.moves[0] = DOWN;
        } else if (strcasecmp(input, "left") == 0) {
            command = MOVE;
            act.moves[0] = LEFT;
        } else {
            command = ERROR;
        }

        act.type = command;
        send_action(sockfd, &act);
        receive_action(sockfd, &act);

        if (act.type == WIN) {
            printf("You escaped!\n");
            print_board(&act);
        } else if (act.type == UPDATE) {
            if (command == START) {
                handle_start(&act);
            } else if (command == MOVE) {
                handle_move(&act);
            } else if (command == MAP) {
                handle_map(&act);
            } else if (command == EXIT) {
                close(sockfd);
                exit(0);
            } else if(command == RESET) {
                handle_reset(&act);
            }
        }
        else if(act.type == GAMEOVER){ // ao enviar comandos e o jogo esta acabado faça nada
        }
         else if (act.type == ERROR) {
            handle_error(&act);
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

void handle_move(struct action *act) {
    print_possible_moves(act);
}

void handle_reset(struct action *act) {
    print_possible_moves(act);
}

void handle_error(struct action *act) {
    printf("%s\n", act->error_message);
}

void handle_start(struct action *act) {
    print_possible_moves(act);
}

void print_possible_moves(struct action* act) {
    printf("Possible moves: ");
    int first = 1; // Variável para verificar se é o primeiro movimento

    for (int i = 0; i < 100; i++) {
        if (act->moves[i] == 0) {
            break;
        }

        // Adicionar vírgula para elementos após o primeiro
        if (!first) {
            printf(", ");
        }

        // Imprimir a direção correspondente
        switch (act->moves[i]) {
            case 1:
                printf("up");
                break;
            case 2:
                printf("right");
                break;
            case 3:
                printf("down");
                break;
            case 4:
                printf("left");
                break;
            default:
                break;
        }

        first = 0;
    }

    printf(".\n");
}

void handle_map(struct action *act) {
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
                printf("X ");
            } else if (value == 1) {
                printf("_ ");
            } else if (value == 0) {
                printf("# ");
            } else if (value == 2) {
                printf("> ");
            } else if (value == 4) {
                printf("? ");
            } else {
                printf("%d ", value);
            }
        }
        printf("\n");
    }
}
