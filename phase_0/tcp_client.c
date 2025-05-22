#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define BUFF_SIZE 10000

int main() {
    //Creating a listening socket
    int client_sock_fd = socket(AF_INET,SOCK_STREAM,0);

    //Client should know the address of the server it should communicate with
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET ;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); //This IP stands for localhost i.e localhost = 127.0.0.1
    server_addr.sin_port = htons(SERVER_PORT); //Don't forget to use htons it translates host byte order to network byte order

    if(connect(client_sock_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) != 0) {
        printf("[ERROR] Failed to connect to tcp server\n");
        exit(1);
    } else {
        printf("[INFO] Connected to tcp server\n");
    }

    while(1) {
        // Get message from client terminal
        char *line = NULL;
        size_t line_len = 0, read_n;
        read_n = getline(&line,&line_len,stdin); //Try using "man getline" in terminal.
        
        send(client_sock_fd,line,read_n,0); //Try man getline in terminal

        char buff[BUFF_SIZE];
        memset(buff,0,BUFF_SIZE);

        read_n = recv(client_sock_fd,buff,sizeof(buff),0);

        if(read_n <= 0 ) {
             printf("[INFO] Error occured. Closing server\n");
            close(client_sock_fd);
            exit(1);
        } else {
            printf("[SERVER MESSAGE] %s\n", buff);
        }
    }

    return 0 ;
}