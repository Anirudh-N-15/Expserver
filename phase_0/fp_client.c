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

void send_file(int sock_fd) {
    //Open the file from which data is being read
    FILE * fptr ;
    const char * filename = "t1.txt" ;
    fptr = fopen(filename,"r"); //Read only mode 

    if(fptr == NULL) {
        perror("[-]Error in opening file.");
        exit(EXIT_FAILURE);
    }

    char data[BUFF_SIZE] = {0};
    printf("[INFO] Sending data to server...\n");

    while(fgets(data,BUFF_SIZE,fptr) != NULL) {
        if(send(sock_fd,data,sizeof(data),0) == -1) {
            perror("[-]Error in sending data.");
            fclose(fptr); // Ensure file is closed on error
            exit(EXIT_FAILURE);
        }
        printf("[FILE DATA] %s", data);
        bzero(data, BUFF_SIZE); // clear the buffer
    }

    printf("[INFO] File data sent successfully.\n");
    fclose(fptr); // Close the file after sending
}


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

    send_file(client_sock_fd);

    close(client_sock_fd);

    return 0 ;
}