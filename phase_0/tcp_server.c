#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10 

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

    int epoll_fd = epoll_create1(0);
    struct epoll_event event, events[MAX_EPOLL_EVENTS];

    event.data.fd = listen_sock_fd ; 
    event.events = EPOLLIN ;                                //Specifies that it is interested in read events of socket
    epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listen_sock_fd,&event);//adds the listening socket to epoll 

    while(1) {
        printf("[DEBUG] Epoll wait\n");
        int n_ready_fds = epoll_wait(epoll_fd,events,MAX_EPOLL_EVENTS,-1);

        for(int i = 0; i < n_ready_fds; i++) {
            int curr_fd = events[i].data.fd ;

            if(curr_fd == listen_sock_fd) { //Event is on a listen socket
                // Creating an object of struct socketaddr_in
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);//Initialising the length to that of client address
                int conn_sock_fd = accept(listen_sock_fd, (struct sockaddr *)&client_addr, &client_addr_len);   //Accept connection 
                printf("[INFO] Client connected to server\n");

                struct epoll_event client_event ;
                client_event.data.fd = conn_sock_fd ;
                client_event.events = EPOLLIN ;
                epoll_ctl(epoll_fd,EPOLL_CTL_ADD,conn_sock_fd,&client_event);  //Add the client socket to epoll 
            } else { //It is a connection socket
                char buffer[BUFF_SIZE];
                memset(buffer,0,BUFF_SIZE);

                ssize_t read_n = recv(curr_fd, buffer, sizeof(buffer), 0);

                if(read_n <= 0) {
                    printf("[INFO] Client disconnected.\n");
                    close(curr_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL); //Delete the curr_fd
                } else {
                    printf("[CLIENT MESSAGE] %s", buffer);
                    reverseString(buffer);
                    send(curr_fd, buffer, read_n, 0);   // Sending reversed string to client
                }
            }
        }
    }
    close(listen_sock_fd);
    return 0;
}