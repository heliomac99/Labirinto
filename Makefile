# Variáveis
CC = gcc
CFLAGS = -Wall -Wextra -g
BIN_DIR = bin
SERVER_SRC = server.c
CLIENT_SRC = client.c
SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client

# Alvo padrão (executado ao chamar apenas `make`)
all: $(SERVER_BIN) $(CLIENT_BIN)

# Compilar o servidor
$(SERVER_BIN): $(SERVER_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER_BIN)

# Compilar o cliente
$(CLIENT_BIN): $(CLIENT_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)

# Limpar binários
clean:
	rm -rf $(BIN_DIR)

# Rodar o servidor
run-server: $(SERVER_BIN)
	$(SERVER_BIN) input.txt

# Rodar o cliente
run-client: $(CLIENT_BIN)
	$(CLIENT_BIN) 127.0.0.1 51511

.PHONY: all clean run-server run-client
