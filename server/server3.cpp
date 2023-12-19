#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstring>
#include <pthread.h>
#include <map>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <vector>
#include <fstream>
#include "WordManager.cpp"
#include <sys/select.h>
#include <string>


#define BUFFER_SIZE 1024
#define MAX_PLAYER_COUNT 2
#define MAX_ROUNDS_COUNT 5
#define PLAYER_CHANCES 7

struct ClientInfo {
    int game_id;
    std::string nickname;
    int client_socket;
};

std::mutex mutex; 
std::condition_variable cv; 
bool isGameThreadReady = false;

struct GameInfo {
    int id;
    int current_players_count;
    int current_round;
    pthread_t thread;
    std::queue<ClientInfo *> joinRequests; 
    std::vector<int> connectedClients; 

};

std::map<int, GameInfo *> activeGames;
std::map<int, ClientInfo *> clientInfoMap;


template <typename K, typename V>
std::string serializeMap(const std::map<K, V>& inputMap) {
    std::string result;
    for (const auto& entry : inputMap) {
        result += entry.first + ":" + std::to_string(entry.second) + ",";
    }
    return result;
}


void sendChancesToAllClients(GameInfo *game, const std::map<std::string, int> &chances) {
    std::string serialized_chances = serializeMap(chances);
    for (int client_socket : game->connectedClients) {
        send(client_socket, serialized_chances.c_str(), serialized_chances.length(), 0);
    }
}


void informAllClients(GameInfo *game, const std::string &message) {
    for (int client_socket : game->connectedClients) {
        send(client_socket, message.c_str(), message.length(), 0);
    }
}

std::string readMsg(int client_socket) {
    char buffer[BUFFER_SIZE];
    std::string msg;
    
    while (true) {
        ssize_t bytes_received = read(client_socket, buffer, sizeof(buffer));
        if (bytes_received <= 0) {
            close(client_socket);
            return "";
        }

        msg += std::string(buffer, bytes_received);
        size_t newline_pos = msg.find('\n');
        std::cout << newline_pos << std::endl;
        if (newline_pos != std::string::npos) {
            msg = msg.substr(0, newline_pos);
            break;
        }
    }

    std::cout << "Function gives: " << msg << std::endl;
    return msg;

    }
    

void *gameServer(void *arg) {
    GameInfo *game = static_cast<GameInfo *>(arg);

    game->current_players_count = 0;
    game->current_round = 0;

    // To store for priettier output
    std::map<int, std::string> sock_to_nickname_map;

    // ranking
    std::map<std::string, int> ranking;

   
    std::map<std::string, int> chances;

    // mutex to reading msgs sequentionally


    // Game logic
    while (true) {
        // for select
        fd_set readfds, readfds_copy;
        FD_ZERO(&readfds);
        int max_sd = 0;
        // for words generator
        WordManager wordManager("words.txt");


        if(game->current_round == 0){
            // Check for join requests
            if (!game->joinRequests.empty()) {
                ClientInfo *joinRequest = game->joinRequests.front();
                game->joinRequests.pop();

                // Access the information about the client
                int game_id = joinRequest->game_id;
                std::string nickname = joinRequest->nickname;
                int client_socket = joinRequest->client_socket;
                sock_to_nickname_map[client_socket] = nickname;


                printf("Processing join request: Game ID %d, Nickname %s, Client Socket %d\n", game_id, nickname.c_str(), client_socket);

                if (game->current_players_count < MAX_PLAYER_COUNT) {
                    std::string response = "ok\n"; 
                    printf("Client can join\n");
                    send(client_socket, response.c_str(), response.length(), 0);
                    game->current_players_count += 1;
                    game->connectedClients.push_back(client_socket);
                    ranking[nickname] = 0;
                    chances[nickname] = PLAYER_CHANCES;
                } else {
                    std::string response = "no\n"; 
                    printf("Client cannot join\n");
                    send(client_socket, response.c_str(), response.length(), 0);
                }
                delete joinRequest;
            }
        }
        

        if (game->current_players_count == MAX_PLAYER_COUNT) {
            if(game->current_round == 0){
                {
                std::lock_guard<std::mutex> lock(mutex);
                isGameThreadReady = true;
                printf("Game ready\n");
                }
                cv.notify_one();
                // Inform all clients that the game is starting
                informAllClients(game, "s\n");
                printf("Informed all clients about the start\n");

                // get players nicknames
                std::string nicknames;
                for (const auto& entry : sock_to_nickname_map) {
                    nicknames += entry.second + " ";
                }
                if (!nicknames.empty()) {
                    nicknames.pop_back();
                }
                nicknames+="\n";
                informAllClients(game, nicknames);

                //select setup
                for (int client_socket : game->connectedClients) {
                    FD_SET(client_socket, &readfds);

                    if(client_socket > max_sd){
                        max_sd = client_socket;
                    }
                }     
            }
                 
            
            while(game->current_round < MAX_ROUNDS_COUNT){
                readfds_copy = readfds;
                if(game->current_round == 0){
                    
                    informAllClients(game, "n\n");

                    game -> current_round += 1;   

                    std::string randomWord = wordManager.getRandomWord();
                    randomWord += "\n";
                    std::cout << "Random word: " << randomWord << std::endl;
                    informAllClients(game, randomWord);
                    
                }
                              

                int clientActivity = select(max_sd + 1, &readfds_copy, NULL, NULL, NULL);
                if(clientActivity < 0){
                    perror("select error");
                    exit(EXIT_FAILURE);
                } else if(clientActivity > 0){
                    for (int client_socket : game->connectedClients) {
                        if(FD_ISSET(client_socket, &readfds_copy)){
                            char buffer[BUFFER_SIZE];

                            std::string msg = readMsg(client_socket);
                            std::cout << "Received data:" << msg << " from:  " << sock_to_nickname_map[client_socket] << std::endl;
                            if(msg == "w"){
                                std::cout << sock_to_nickname_map[client_socket] << " won the round" << std::endl; 
                                ranking[sock_to_nickname_map[client_socket]] += 10;
                                if(game->current_round == MAX_ROUNDS_COUNT){
                                    informAllClients(game, "e\n");                                 
                                }
                                else{
                                    informAllClients(game, "n\n");

                                    std::string randomWord = wordManager.getRandomWord();
                                    randomWord += "\n";
                                    game->current_round += 1;
                                    informAllClients(game, randomWord);
                                } 
                             
                            } else if(msg == "+"){
                                std::cout << sock_to_nickname_map[client_socket] << " guessed the letter" << std::endl; 
                                ranking[sock_to_nickname_map[client_socket]] += 1;
                              
                            
                            } else if(msg == "-"){
                                std::cout << sock_to_nickname_map[client_socket] << "failed to guess the later " << std::endl; 
                                chances[sock_to_nickname_map[client_socket]] -= 1;
                                informAllClients(game, "-\n");
                                std::string client_nickname = sock_to_nickname_map[client_socket];
                                std::string client_chances = std::to_string(chances[sock_to_nickname_map[client_socket]]);
                                std::string msg_to_send = client_nickname + " " + client_chances  +"\n";
                                informAllClients(game, msg_to_send);
                                //sendChancesToAllClients(game, chances); 
                            }
                        }
                    }
                }
            }
        }  
    }
    return nullptr;
}



void *handleClient(void *arg) {
    int client_socket = *(int *)arg;

    char buffer[BUFFER_SIZE];
    // TO DO:
    // use read untill new line (readMsg - and add error handling)
    int valread = read(client_socket, buffer, sizeof(buffer));
    if (valread == 0) {
        printf("Client not connected\n");
        close(client_socket);
        return nullptr;
    }

    buffer[valread] = '\0';
    std::string msg(buffer);

    // std::string msg = readMsg(client_socket);

    size_t delimiter_pos = msg.find(' ');
    if (delimiter_pos != std::string::npos) {
        std::string nickname = msg.substr(0, delimiter_pos);
        printf("Nickname: %s\n", nickname.c_str());
        int game_id = atoi(msg.substr(delimiter_pos + 1).c_str());
        printf("Game id: %d\n", game_id);

        // Connect to game server or create if not exists
        mutex.lock();
        GameInfo *game = nullptr;
        if (activeGames.find(game_id) != activeGames.end()) {
            game = activeGames[game_id];
            printf("there is a game");
        } else {
            game = new GameInfo;
            game->id = game_id;
            game->current_players_count = 0; 
            activeGames[game_id] = game;
            
             printf("Active Games:\n");
        for (const auto& entry : activeGames) {
            printf("Game ID: %d, Current Players Count: %d\n", entry.first, entry.second->current_players_count);
        }
            // Create a new thread for the game server
            if (pthread_create(&game->thread, nullptr, gameServer, (void *)game) != 0) {
                perror("pthread_create error");
            
            }

        }

        // Store client info
        ClientInfo *clientInfo = new ClientInfo;
        clientInfo->game_id = game_id;
        clientInfo->nickname = nickname;
        clientInfo->client_socket = client_socket;
        clientInfoMap[client_socket] = clientInfo;
        printf("Client info stored\n");

        game->joinRequests.push(clientInfo);
        printf("Join request added to the queue\n");

        mutex.unlock();
    } else {
        printf("Invalid data format received from the client.\n");
        close(client_socket);
    }

    return nullptr;
}


int main() {
    int on = 1;
    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2000); 
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sockfd, 5) == -1) {
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 2000...\n");

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int *client_socket = new int(accept(server_sockfd, (struct sockaddr *)&client_addr, &client_addr_len));
        if (*client_socket == -1) {
            perror("accept error");
            continue;
        }

        printf("New client connected\n");

        // new thread for each client
        pthread_t client_thread;
        if (pthread_create(&client_thread, nullptr, handleClient, (void *)client_socket) != 0) {
            perror("pthread_create error");
           
        }

 
        pthread_detach(client_thread);
    }

    close(server_sockfd);
    return 0;
}
