#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <vector>
#include <sys/epoll.h>
#include <map>
#include <pthread.h>
#include <mutex>
#include <condition_variable>

#define BUFFER_SIZE 1024
#define NICKNAME_SIZE 64
#define MAX_CLIENTS 3
#define MAX_ROUNDS 5


struct ClientInfo{
    int socket;
    sockaddr_in address;
    std::string name;
    int game_id;
    int score;
};

struct Game{
    int id;
    std::vector<ClientInfo *> players;
    int round;
    int players_count;
    int current_players_count;
    std::vector<std::string> words;
    int epoll_fd;
    int status;
    std::mutex mutex;
    std::condition_variable players_ready;
};

std::map<int, Game*> activeGames;


void gameLogic(Game * game){
    //game logic
    printf("Game %d started\n", game->id);

//Need some sort of lock, causes segfault
    // for(int i = 0; i<game->current_players_count; i++){
    //     send(game->players[i]->socket, "Game started.\n", 14, 0);
    // }

    
    
    game->status = 2;
}

void * handleClient(void * arg){
    ClientInfo * client = (ClientInfo *)arg;
    char buffer[BUFFER_SIZE];
    char nickname[NICKNAME_SIZE];
    char gameId[2];
    char playersCount[2];


    int valread = read(client->socket, buffer, sizeof(buffer));
    if (valread == 0) {
        printf("Client not connected\n");
    }

    buffer[valread] = '\0';
    std::string received_data(buffer);

    // Split the received data into nickname and game ID using a delimiter (e.g., space)
    size_t delimiter_pos = received_data.find(' ');
    if (delimiter_pos != std::string::npos) {
        client->name = received_data.substr(0, delimiter_pos);
        printf("Nickname: %s\n", client->name.c_str());
        client->game_id = atoi(received_data.substr(delimiter_pos + 1).c_str());
        printf("Game id: %d\n", client->game_id); 
    } else {
        // Handle the case where there is no delimiter or the format is incorrect
        printf("Invalid data format received from the client.\n");
        // You may want to close the connection or handle the error appropriately
    }




    Game* game;
    if (activeGames.find(client->game_id) != activeGames.end()) {
        // Game already exists 
        game = activeGames[client->game_id];
        game->current_players_count++;
        if (game->current_players_count == game->players_count) {
        // Notify all waiting players that the required number of players has been reached
        game->players_ready.notify_all();

        for (int i = 0; i < game->current_players_count; i++) {
        send(game->players[i]->socket, "GameStart", 9, 0);
    }
    }
        }else {        
        // Create a new game
        game = new Game;
        game->id = client->game_id;
        activeGames[client->game_id] = game;
        printf("game created");
        //send(client->socket, "Game created.\n", 14, 0);
        //send(client->socket, "Enter the number of players.\n", 29, 0);
        // valread = read(client->socket, playersCount, sizeof(playersCount));
        // if(valread == 0){
        //     printf("Client not connected\n");
        // }
        // playersCount[valread] = '\0';
        game->players_count = MAX_CLIENTS;  //atoi(playersCount); TO PUZNIEJ
        game->current_players_count = 1;
        
        printf("Players count: %d\n", game->players_count);
    }

    game->players.push_back(client);


    //send(client->socket, "Waiting for other players...\n", 29, 0);

    // Wait for all players to join

    std::unique_lock<std::mutex> lock(game->mutex);
    game->status = 0;
    game->players_ready.wait(lock, [game] { return game->current_players_count == game->players_count; });
    

    if(game->current_players_count == game->players_count){
        game->status = 1;
    }

    while (game->status == 0)
    {
        sleep(1);
    }
    
    if (game->status == 1){
        gameLogic(game);
    }
    game->status = 2;
    printf("Game %d finished\n", game->id); 
    printf("Game %d status: %d\n", game->id, game->status);

    


    while(true){
        valread = read(client->socket,buffer, sizeof(buffer));
        if(valread == 0){
            printf("%s disconnected\n", client->name.c_str());
            break;
        }
        buffer[valread] = '\0';

        printf("%s: %s\n", client->name.c_str(), buffer);
    }

    close(client->socket);
    delete client;
    return nullptr;
}




int main(int argc, char * argv[]){

    int PORT = atoi(argv[1]);
    printf("PORT: %d\n", PORT);
    int client_sockets[MAX_CLIENTS] = {0};

    char buffer[BUFFER_SIZE];

    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sockfd == -1){
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if(listen(server_sockfd, 5) == -1){
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    int client_count = 0;
    pthread_t threads [MAX_CLIENTS];

    while (true) {
        // New client connection
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("accept error");
            exit(EXIT_FAILURE);
        }

        printf("New client connected\n");

        // Increment client_count only if it's less than MAX_CLIENTS
        if (client_count < MAX_CLIENTS) {
            client_count++;
            client_sockets[client_socket] = client_socket;
            ClientInfo *client = new ClientInfo;
            client->socket = client_socket;
            client->address = client_addr;
            if (pthread_create(&threads[client_count - 1], nullptr, handleClient, (void *)client) != 0) {
                perror("pthread_create error");
                close(client_socket);
                delete client;
                exit(EXIT_FAILURE);
            }
        }

        if (client_count == MAX_CLIENTS) {
            printf("Max clients reached\n");
            break;
        }
    }

    for (int i = 0; i < client_count; i++){
        pthread_join(threads[i], nullptr);
    }


    close(server_sockfd);
    return 0;

}
