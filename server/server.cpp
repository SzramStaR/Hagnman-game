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
#include "WordManager.h"
#include <sys/select.h>
#include <string>
#include <algorithm>


#define BUFFER_SIZE 1024
#define MAX_PLAYERS_COUNT 2
#define MAX_ROUNDS_COUNT 5
#define PLAYER_CHANCES 7

struct ClientInfo {
    int game_id;
    std::string nickname;
    int client_socket;
};

std::mutex mutex; 
std::condition_variable cv; 



struct GameInfo {
    int id;
    int current_players_count;
    int current_round;
    int max_rounds_count;
    int max_players_count;
    pthread_t thread;
    std::queue<ClientInfo *> joinRequests; 
    std::vector<int> connectedClients; 
    std::condition_variable newPlayerCv;
    std::condition_variable gameReadyCv;
    std::mutex gameLock;
    std::mutex gameStartLock;
    std::mutex gameEndLock;
    std::mutex roundLock;
    bool gameBool = false;
    bool isGameReady = false;
    bool roundAlreadyWon = false;

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

int getMaxGameIdValue() {
    int maxId = 0;  

    for (const auto& gamePair : activeGames) {
        int currentId = gamePair.second->id;
        maxId = std::max(maxId, currentId);
    }

    return maxId;
}

void sendChancesToAllClients(GameInfo *game, const std::map<std::string, int> &chances) {
    std::string serialized_chances = serializeMap(chances);
    for (int client_socket : game->connectedClients) {
        send(client_socket, serialized_chances.c_str(), serialized_chances.length(), 0);
    }
}


void informAllClients(GameInfo *game, const std::string &message) {
    for(auto it = game->connectedClients.begin(); it != game->connectedClients.end();){
        int client_socket = *it;
        int check = send(*it, message.c_str(), message.length(), 0);
        if (check == -1) {
            printf("Client disconnected\n");
            it = game->connectedClients.erase(it);
        } else {
            ++it;
        }

    }
}

class MessageParser{
    // std::string buffer;
public:
    std::string buffer;
    void addData(const std::string& data){
        buffer += data;
    }
    
    std::string getNextMessage(){
        size_t newline_pos = buffer.find('\n');
        if(newline_pos != std::string::npos){
            std::string msg = buffer.substr(0,newline_pos);
            buffer = buffer.substr(newline_pos+1);
            return msg;
        }else{
            return "";
        }
    }
};

std::string readMsg(int client_socket, MessageParser& parser) {
    char buffer[BUFFER_SIZE];
    
    ssize_t bytes_received = read(client_socket, buffer, sizeof(buffer));
    if (bytes_received <= 0) {
        close(client_socket);
        return "";
    }

    parser.addData(std::string(buffer, bytes_received));
    std::string msg = parser.getNextMessage();
    if(!msg.empty()){
        std::cout <<"Function gives: "<<msg<<std::endl;
        return msg;
    }

    return "";
}
    
    

void *gameServer(void *arg) {
    GameInfo *game = static_cast<GameInfo *>(arg);
    MessageParser parser;

    game->current_players_count = 0;
    game->current_round = 0;

   // id, nickname map
    std::map<int, std::string> sock_to_nickname_map;

    // ranking
    std::map<std::string, int> ranking;

    std::map<std::string, int> chances;

    // mutex to reading msgs sequentionally


    std::unique_lock<std::mutex> lck(game->gameLock);
    game->newPlayerCv.wait(lck , [&]{return game->gameBool;});

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

                if (game->current_players_count < game->max_players_count) {
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
        

        if (game->current_players_count == game->max_players_count) {
            if(game->current_round == 0){
                {
                std::lock_guard<std::mutex> lock(mutex);
                printf("Game ready\n");
                game->isGameReady = true;
                game->gameReadyCv.notify_one();
                }
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
            std::unique_lock<std::mutex> lckStart(game->gameStartLock);
            game->gameReadyCv.wait(lckStart, [&]{return game->isGameReady;});
            //if moved from the while
            if(game->current_round == 0){
                    
                    informAllClients(game, "n\n");

                    game -> current_round += 1;   

                    std::string randomWord = wordManager.getRandomWord();
                    randomWord += "\n";
                    std::cout << "Random word: " << randomWord << std::endl;
                    informAllClients(game, randomWord);
                    
                }         
            while(game->current_round <= game->max_rounds_count && game->current_players_count > 0){
                readfds_copy = readfds;
                int clientActivity = select(max_sd + 1, &readfds_copy, NULL, NULL, NULL);
                if(clientActivity < 0){
                    perror("select error");
                    exit(EXIT_FAILURE);
                } else if(clientActivity > 0){
                    std::vector<int> disconnectedClients;
                    for (int client_socket : game->connectedClients) {
                        if(FD_ISSET(client_socket, &readfds_copy)){
                            char buffer[BUFFER_SIZE];
                                //case jak wyjdą, niekoniecncznie muszą dostać f
                                if (game->current_players_count == 1) {
                                        int lastPlayerSocket = sock_to_nickname_map.rbegin()->first;
                                        printf("Last player nickname: %s\n", sock_to_nickname_map.rbegin()->second.c_str());
                                    }

                                std::string msg = readMsg(client_socket, parser);
                                if (msg.empty()) {
                                    printf("Client %s disconnected\n", sock_to_nickname_map[client_socket].c_str());
                                    FD_CLR(client_socket, &readfds);
                                    close(client_socket);
                                    sock_to_nickname_map.erase(client_socket);
                                    disconnectedClients.push_back(client_socket);
                                    game->current_players_count -= 1;
                                    continue;
                                }
                                std::cout << "Received data:" << msg << " from:  " << sock_to_nickname_map[client_socket] << std::endl;
                                if(msg == "w"){
                                    std::lock_guard<std::mutex> lock(game->roundLock);
                                    if(game->roundAlreadyWon){
                                        continue;
                                    }
                                    game->roundAlreadyWon = true;
                                    std::cout << sock_to_nickname_map[client_socket] << " won the round" << std::endl; 
                                    ranking[sock_to_nickname_map[client_socket]] += 10;
                                    parser.buffer = "";
                                    if(game->current_round == game->max_rounds_count){
                                        informAllClients(game, "e\n");     
                                        game->roundAlreadyWon = false;                            
                                    }
                                    else{
                                        informAllClients(game, "n\n");

                                        std::string randomWord = wordManager.getRandomWord();
                                        randomWord += "\n";
                                        game->current_round += 1;
                                        informAllClients(game, randomWord);
                                        game->roundAlreadyWon = false;
                                    }                            
                                } else if(msg == "+"){
                                    std::cout << sock_to_nickname_map[client_socket] << " guessed the letter" << std::endl; 
                                    ranking[sock_to_nickname_map[client_socket]] += 1;                            
                                    parser.buffer = "";
                                } else if(msg == "-"){
                                    std::cout << sock_to_nickname_map[client_socket] << "failed to guess the later " << std::endl; 
                                    chances[sock_to_nickname_map[client_socket]] -= 1;
                                    informAllClients(game, "-\n");
                                    std::string client_nickname = sock_to_nickname_map[client_socket];
                                    std::string client_chances = std::to_string(chances[sock_to_nickname_map[client_socket]]);
                                    std::string msg_to_send = client_nickname + " " + client_chances  +"\n";
                                    informAllClients(game, msg_to_send);
                                    //sendChancesToAllClients(game, chances); 
                                    parser.buffer = "";
                                }
                                else if(msg == "f"){
                                    std::cout << sock_to_nickname_map[client_socket] << " lost the game " << std::endl; 
                                    
                                    close(client_socket);
                                    sock_to_nickname_map.erase(client_socket);
                                    game->current_players_count -= 1;
                                    disconnectedClients.push_back(client_socket);
                                    FD_CLR(client_socket, &readfds);
                                    
                                    parser.buffer = "";
                                    if (game->current_players_count == 1) {
                                            int lastPlayerSocket = sock_to_nickname_map.rbegin()->first;
                                            printf("Last player nickname: %s\n", sock_to_nickname_map.rbegin()->second.c_str());
                                            send(lastPlayerSocket, "w\n", 2, 0);
                                            
                                        }
                                    
                                }
                                
                            
                            
                        }
                    }
                    for (int client_socket : disconnectedClients) {
                        game->connectedClients.erase(std::remove(game->connectedClients.begin(), game->connectedClients.end(), client_socket), game->connectedClients.end());
                    }
                }
            }
            
            std::lock_guard<std::mutex> lock(game->gameEndLock);
            for(std::map<std::string, int>::iterator it = ranking.begin(); it != ranking.end(); ++it){
                std::string client_nickname = it->first;
                std::string client_score = std::to_string(it->second);
                std::string msg_to_send = client_nickname + " " + client_score + "\n";
                printf("Ranking: %s\n", msg_to_send.c_str());
                informAllClients(game, msg_to_send);
            }
            printf("Game %d closing...\n", game->id);
            activeGames.erase(game->id);
            for(auto& socket:game->connectedClients){ 
                close(socket);
            }   
            pthread_detach(game->thread);
        }  
    }
    return nullptr;
}



void *handleClient(void *arg) {
    int max_players_count = MAX_PLAYERS_COUNT;
    int max_rounds_count = MAX_ROUNDS_COUNT;

    int client_socket = *(int *)arg;

    char buffer[BUFFER_SIZE];
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
        
        if(game_id == 0){
            int new_game_id = getMaxGameIdValue() + 1;
            game_id = new_game_id;
            std::string message = std::to_string(new_game_id) + "\n";
            ssize_t send_result = send(client_socket, message.c_str(), message.length(), 0);
            if (send_result == -1) {
                perror("Error sending data");
                close(client_socket);
                mutex.unlock();
                return nullptr;
            }
            
            int valread = read(client_socket, buffer, sizeof(buffer));
            if (valread == 0) {
                printf("Client not connected\n");
                close(client_socket);
                return nullptr;
            }
            buffer[valread] = '\0';
            std::string msg(buffer);

            size_t delimiter_pos = msg.find(' ');
            if (delimiter_pos != std::string::npos) {
                max_players_count = atoi(msg.substr(0, delimiter_pos).c_str());
                printf("max players: %d\n", max_players_count);
                max_rounds_count = atoi(msg.substr(delimiter_pos + 1).c_str());
            }

        }

        if (activeGames.find(game_id) != activeGames.end()) {
            game = activeGames[game_id];
            printf("there is a game");
        } else {
            game = new GameInfo;
            game->id = game_id;
            game->current_players_count = 0; 
            game->max_players_count = max_players_count;
            game->max_rounds_count = max_rounds_count;
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
        game->gameBool = true; 
        game->newPlayerCv.notify_one();
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