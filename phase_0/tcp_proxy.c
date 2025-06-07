#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <stdbool.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10 
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 10

int listen_sock_fd,epoll_fd;
int route_table[MAX_SOCKS][2], route_table_size = 0;
struct epoll_event events[MAX_EPOLL_EVENTS];

int find_pair(int sock_fd, bool flag){
    int idx = flag?0:1, res = flag?1:0;

    for(int i = 0; i<route_table_size; i++){
        if(route_table[i][idx]==sock_fd){
            return route_table[i][res];
        }
    }
    return -1;
}

int get_fd_role(int fd) {
    for(int i=0;i<route_table_size;i++) {
        if(route_table[i][0] == fd) return 1; //client
        if(route_table[i][1] == fd) return 0; //upstream
    }
    return -1; //Not found.
}

void loop_attach(int epoll_fd,int fd,int events) {
    //Attaches file descriptor to epoll
    struct epoll_event event ;
    event.events = events ; //EPOLLIN(fd has data ready for reading) & EPOLLOUT(ready for writing)
    event.data.fd = fd ;

    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd,&event) < 0) {
        perror("epoll_ctl: loop_attach");
        exit(EXIT_FAILURE);
    }
}

void handle_upstream(int upstream_sock_fd) {
    char buffer[BUFF_SIZE];
    memset(buffer,0,BUFF_SIZE);

    int read_n = recv(upstream_sock_fd,buffer,sizeof(buffer),0);

    if(read_n <= 0) {
        printf("[INFO] Client disconnected.\n");
        epoll_ctl(epoll_fd,EPOLL_CTL_DEL,upstream_sock_fd,NULL);
        close(upstream_sock_fd);
        return;
    } 

    /* find the right client socket from the route table */
    int client_sock_fd = find_pair(upstream_sock_fd,false);

    // sending upstream message to client
    int bytes_written = 0;
    int message_len = read_n;
    while(bytes_written < message_len) {
        int n = send(client_sock_fd,buffer + bytes_written,message_len - bytes_written,0);
        bytes_written += n;
    }
}

void handle_client(int conn_sock_fd) {
    char buffer[BUFF_SIZE];
    memset(buffer,0,BUFF_SIZE);

    int read_n = recv(conn_sock_fd,buffer,sizeof(buffer),0);

    if(read_n <= 0) {
        printf("[ERROR] Error encountered, Closing connection (handle client)\n");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn_sock_fd, NULL);
        close(conn_sock_fd);
        return;
    } 
    printf("[CLIENT MESSAGE] %s\n",buffer);

    int upstream_sock_fd = find_pair(conn_sock_fd,true); //find the right upstream socket from the route table 

    // sending client message to upstream
    int bytes_written = 0;
    int message_len = read_n;
    while(bytes_written < message_len) {
        int n = send(upstream_sock_fd,buffer + bytes_written,message_len - bytes_written,0);
        bytes_written += n;
    }
}

int connect_upstream() {
    int upstream_sock_fd = socket(AF_INET,SOCK_STREAM,0);

    struct sockaddr_in upstream_addr ;
    upstream_addr.sin_family = AF_INET;
    upstream_addr.sin_port = htons(UPSTREAM_PORT);
    upstream_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(connect(upstream_sock_fd,(struct sockaddr *)&upstream_addr,sizeof(upstream_addr)) < 0) {
        perror("connect");
        close(upstream_sock_fd);
        return -1;
    }

    return upstream_sock_fd;
}

void accept_connection(int listen_sock_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int conn_sock_fd = accept(listen_sock_fd,(struct sockaddr *)&client_addr,&client_len);

    printf("[INFO] Client connected to the server.\n");

    loop_attach(epoll_fd,conn_sock_fd,EPOLLIN);

    int upstream_sock_fd = connect_upstream();

    if(upstream_sock_fd == -1){
        close(conn_sock_fd);
        return;
    }

    loop_attach(epoll_fd,upstream_sock_fd,EPOLLIN);

    route_table[route_table_size][0] = conn_sock_fd;
    route_table[route_table_size][1] = upstream_sock_fd;
    route_table_size++ ; 
}

int create_loop() {
    //Returns a new epoll instance 
    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }
    return epoll_fd;
}

int create_server() {
    //Creates a listening socket and returns it(Should create using socket() , setsockopt(), bind(), 
    //listen() syscalls and only then return it).Else will get error if only used socket().
    int listen_sock_fd = socket(AF_INET,SOCK_STREAM,0);

    if(listen_sock_fd < 0) {
        perror("Socket");
        exit(EXIT_FAILURE);
    }
    int enable = 1;
    //To avoid "address already in use" ERROR
    setsockopt(listen_sock_fd,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(int)); //To allow reuse of this port even in TIME_WAIT state
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);
    
    if(bind(listen_sock_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) < 0) {
        perror("Bind");
        exit(EXIT_FAILURE);
    }

    if(listen(listen_sock_fd,MAX_ACCEPT_BACKLOG) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("[INFO]\tServer listening on port %d\n", PORT);
    return listen_sock_fd ; // Ready to be used with accept
}

void loop_run(int epoll_fd) {
    /* infinite loop and processing epoll events */
    while(1) {
        printf("[DEBUG] Epoll Wait\n");
        int n_ready_fds = epoll_wait(epoll_fd,events,MAX_EPOLL_EVENTS,-1);

        for(int i=0;i<n_ready_fds;i++) {
            int curr_fd = events[i].data.fd ;

            if(curr_fd == listen_sock_fd) {//Event on listen socket
                accept_connection(listen_sock_fd);
                continue; // Not sure about this
            } 
            /*Checks if it is a client_sock_fd or upstream_sock_fd*/
            int role = get_fd_role(curr_fd);

            if(role == 1) {
                handle_client(curr_fd);
            } else {
                handle_upstream(curr_fd);
            }
        }
    }
}

int main() {
    listen_sock_fd = create_server();
    epoll_fd = create_loop();
    loop_attach(epoll_fd,listen_sock_fd,EPOLLIN);
    loop_run(epoll_fd);
    return 0;
}