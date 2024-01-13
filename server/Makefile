CC = g++

CFLAGS = -std=c++11

SERVER_SRC = server.cpp

OUTPUT = server

all:$(OUTPUT)

$(OUTPUT): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(OUTPUT) $(SERVER_SRC)

clean:
	rm -f $(OUTPUT)

