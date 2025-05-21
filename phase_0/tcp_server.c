#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5

void reverseString(char * str) {
    int n = strlen(str);
    for(int i = 0; i < (n/2); i++) {
        int temp = str[i];
        str[i] = str[n-i-1];
        str[n-1-i] = temp ;
    }
}

int main() {
    int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    //To be able to reuse this socket we SO_REUSEADDR even if it goes to TIME_WAIT state.
    //Setting sock opt reuse addr
    int enable = 1;
    setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    // Creating an object of struct socketaddr_in
    struct sockaddr_in server_addr;

    // Setting up server addr
    server_addr.sin_family = AF_INET;                   //Uses IpV4
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);    //used to convert the IP from host byte order to network byte order
    server_addr.sin_port = htons(PORT);                 //Used to convert port number from host byte order to network byte order

    // Binding listening sock to port
    bind(listen_sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Starting to listen
    listen(listen_sock_fd, MAX_ACCEPT_BACKLOG); //If connection is full, further connection attempts will be declined until entry available
    printf("[INFO] Server listening on port %d\n", PORT);

    // Creating an object of struct socketaddr_in
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    // Accept client connection
    int conn_sock_fd = accept(listen_sock_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    printf("[INFO] Client connected to server\n");

    while(1) {
        //Created buffer to be able to store message from client
        char buff[BUFF_SIZE];
        memset(buff,0,BUFF_SIZE);

        //Read message from client to buffer 
        ssize_t read_n = recv(conn_sock_fd,buff,sizeof(buff),0);

        if(read_n <= 0) { //-1 indicates some err has occurred while 0 indicates client closed the connection
            printf("[INFO] Error occured. Closing server\n");
            close(conn_sock_fd);
            exit(1);            
        }

        printf("[CLIENT MESSAGE] %s\n", buff);

        reverseString(buff);

        send(conn_sock_fd,buff,read_n,0);
    }


}