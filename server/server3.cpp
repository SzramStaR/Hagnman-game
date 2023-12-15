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


#define BUFFER_SIZE 1024
#define MAX_PLAYER_COUNT 2
#define MAX_ROUNDS_COUNT 5

struct ClientInfo {
    int game_id;
    std::string nickname;
    int client_socket;
};

std::mutex mutex; // Declare a mutex
std::condition_variable cv; // Declare a condition variable
bool isGameThreadReady = false; // Variable to indicate whether the game thread is ready

struct GameInfo {
    int id;
    int current_players_count;
    int current_round;
    pthread_t thread;
    std::queue<ClientInfo *> joinRequests; // Queue for join requests
    std::vector<int> connectedClients; 
};

std::map<int, GameInfo *> activeGames;
std::map<int, ClientInfo *> clientInfoMap;

//Przerabianie mapy na stringa
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

void *gameServer(void *arg) {
    GameInfo *game = static_cast<GameInfo *>(arg);

    game->current_players_count = 0;
    game->current_round = 0;

    //To store for priettier output or sth
    std::map<int, std::string> sock_to_nickname_map;

    //ranking
    std::map<std::string, int> ranking;

    //mapa szans dla graczy (eksperymentalnie jako pomysł, możesz zmienić na coś innego)
    std::map<std::string, int> chances;

    // Game logic goes here
    while (true) {
        

        // Check for join requests
        if (!game->joinRequests.empty()) {
            // Process the join request
            ClientInfo *joinRequest = game->joinRequests.front();
            game->joinRequests.pop();

            // Access the information about the client
            int game_id = joinRequest->game_id;
            std::string nickname = joinRequest->nickname;
            int client_socket = joinRequest->client_socket;
            sock_to_nickname_map[client_socket] = nickname;

            printf("Processing join request: Game ID %d, Nickname %s, Client Socket %d\n", game_id, nickname.c_str(), client_socket);

            if (game->current_players_count < MAX_PLAYER_COUNT) {
                std::string response = "ok\n"; // Modify as needed
                printf("Client can join\n");
                send(client_socket, response.c_str(), response.length(), 0);
                game->current_players_count += 1;
                game->connectedClients.push_back(client_socket);
                ranking[nickname] = 0;
                chances[nickname] = 6;
            } else {
                std::string response = "no\n"; // Modify as needed
                printf("Client cannot join\n");
                send(client_socket, response.c_str(), response.length(), 0);
            }

            // Clean up the ClientInfo instance
            delete joinRequest;
        }

        if (game->current_players_count == MAX_PLAYER_COUNT) {
            // Notify the main thread that the game thread is ready
            {
                std::lock_guard<std::mutex> lock(mutex);
                isGameThreadReady = true;
                printf("Game ready\n");
            }
            cv.notify_one();
            

            // Inform all clients that the game is starting
            informAllClients(game, "s\n");
            printf("Informed all clients about the start\n");
            WordManager wordManager("words.txt");

            //select setup
            fd_set readfds, readfds_copy;
            FD_ZERO(&readfds);
            int max_sd = 0;
            for (int client_socket : game->connectedClients) {
                FD_SET(client_socket, &readfds);
                //So that it doesnt have to iterate over all the fdsize
                if(client_socket > max_sd){
                    max_sd = client_socket;
                }
            }

            
            while(game->current_round < MAX_ROUNDS_COUNT){
                //select is destructive so we need to copy it
                readfds_copy = readfds;
                game -> current_round += 1;
                if(game->current_round == 1){
                    
                    informAllClients(game, "n\n");
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
                            int valread = read(client_socket, buffer, sizeof(buffer));
                            if (valread == 0) {
                                std::cout<<sock_to_nickname_map[client_socket] << " disconnected" << std::endl;
                                close(client_socket);
                                return nullptr;
                            }

                            buffer[valread] = '\0';
                            std::string received_data(buffer);
                            std::cout << "Received data:" << received_data << " from:  " << sock_to_nickname_map[client_socket] << std::endl;
                            if(received_data == "w\n"){
                                std::cout << sock_to_nickname_map[client_socket] << " won the round" << std::endl; 
                                ranking[sock_to_nickname_map[client_socket]] += 10;
                                informAllClients(game, "n\n");
                                std::string randomWord = wordManager.getRandomWord();
                                randomWord += "\n";
                                game->current_round += 1;
                                informAllClients(game, randomWord);

                                //sendChancesToAllClients(game, chances);
                            } else if(received_data == "+\n"){
                                std::cout << sock_to_nickname_map[client_socket] << " guessed the letter" << std::endl; 
                                ranking[sock_to_nickname_map[client_socket]] += 1;
                                //informAllClients(game, "+\n");
                                //sendChancesToAllClients(game, chances);
                            } else if(received_data == "-\n"){
                                std::cout << sock_to_nickname_map[client_socket] << "failed to guess the later " << std::endl; 
                                chances[sock_to_nickname_map[client_socket]] -= 1;
                                //informAllClients(game, "-\n");
                                //sendChancesToAllClients(game, chances); 
                            }
                        }
                    }
                }


                // TO DO:
                // po wyslaniu server moglby czekac na jakies wiadomosci od klientow 
                // (nwm czy w innym wontku czy tu )
               
                // jak przyslo 'w' - info ze klient wygral runde to wysyla 
                // wszystkim 'n'- na ktore klienci zaczynaja nowa runde
                // na w server powinien tez robic update rankingu
                // jak przyszlo  + to update rankingu + 1 punkt dla gracza
                // jak - to - 1 szansa dla gracza :
                // trzeba wymyslic co ma server odeslac wszytskim klientom 
                // żebby wiedzieli ktoremo graczowi podmienic fotke w ui czyli np (123, 3)
                // ze gracz 123 ma juz 3 szanse tylko ze poki co klient w petli odbiera 
               


                // to sa fake rzeczy fake rozgrywka bo nic nie odbiera od klientow na razie
                // sleep(10);
                // std::string info = "n\n";
                // informAllClients(game, info);
                // randomWord = wordManager.getRandomWord();
                // randomWord += "\n";
                // std::cout << "Random word drugie : " << randomWord << std::endl;
                // informAllClients(game, randomWord);

                // sleep(30);
                // info = "e\n";
                // informAllClients(game, info);

            }

            //Wysyłanie rankingu
            std::string ranking_string = serializeMap(ranking);
            informAllClients(game, ranking_string);

            
        }  
    }

    return nullptr;
}



void *handleClient(void *arg) {
    int client_socket = *(int *)arg;

    char buffer[BUFFER_SIZE];
    int valread = read(client_socket, buffer, sizeof(buffer));
    if (valread == 0) {
        printf("Client not connected\n");
        close(client_socket);
        return nullptr;
    }

    buffer[valread] = '\0';
    std::string received_data(buffer);

    size_t delimiter_pos = received_data.find(' ');
    if (delimiter_pos != std::string::npos) {
        std::string nickname = received_data.substr(0, delimiter_pos);
        printf("Nickname: %s\n", nickname.c_str());
        int game_id = atoi(received_data.substr(delimiter_pos + 1).c_str());
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
            game->current_players_count = 0; // Initialize with 0, incremented as players join

            activeGames[game_id] = game;
            
             printf("Active Games:\n");
        for (const auto& entry : activeGames) {
            printf("Game ID: %d, Current Players Count: %d\n", entry.first, entry.second->current_players_count);
        }
            // Create a new thread for the game server
            if (pthread_create(&game->thread, nullptr, gameServer, (void *)game) != 0) {
                perror("pthread_create error");
                // Handle thread creation failure, cleanup, and exit if needed
            }

            // Wait for the game thread to be ready
            // printf("Waiting for the game thread to be ready\n");
            // std::unique_lock<std::mutex> lock(mutex);
            // cv.wait(lock, [] { return isGameThreadReady; });
            // printf("Now waiting\n");
        }

        // Store client info
        ClientInfo *clientInfo = new ClientInfo;
        clientInfo->game_id = game_id;
        clientInfo->nickname = nickname;
        clientInfo->client_socket = client_socket;
        clientInfoMap[client_socket] = clientInfo;
        printf("Client info stored\n");

        // Add the join request to the game thread's queue
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
   
    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2000); // Replace with your server port
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

        // Create a new thread for each client
        pthread_t client_thread;
        if (pthread_create(&client_thread, nullptr, handleClient, (void *)client_socket) != 0) {
            perror("pthread_create error");
            // Handle thread creation failure, cleanup, and exit if needed
        }

        // Detach the thread to clean up resources automatically
        pthread_detach(client_thread);
    }

    close(server_sockfd);
    return 0;
}
