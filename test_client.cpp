#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define NICKNAME_SIZE 64

int main(int argc, char * argv[]) {

    int PORT = atoi(argv[1]);
    char buffer[BUFFER_SIZE];
    char nickname[NICKNAME_SIZE];

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    int client_fd;
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("inet_pton error");
        exit(EXIT_FAILURE);
    }

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect error");
        exit(EXIT_FAILURE);
    }

    while (true) {
        // Receive and print messages from the server
        int valread = read(client_fd, buffer, sizeof(buffer)-1);
        if (valread == 0) {
            printf("Server disconnected\n");
            break;
        }
        buffer[valread] = '\0';
        printf("%s\n", buffer);
        

        // Allow the user to input responses
        if (fgets(buffer, sizeof(buffer), stdin) == nullptr) {
            perror("fgets error");
            break;
        }

        // Send the user's response to the server
        send(client_fd, buffer, strlen(buffer), 0);
    }


    close(client_fd);

    return 0;
}
