/**********************
    Rafael Garcia 
***********************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <errno.h>
#include <sys/wait.h>

#define PRINTERR(x) fprintf(stderr, "%s: %s\n", x, strerror(errno))
#define CLSOCK(x) if(close(x) < 0) PRINTERR("Error closing socket")
#define CLS(x) if(close(x) < 0) PRINTERR("Error closing file descriptor")
#define CLEANARGS for(int i = 0; i < num_args; ++i) free(args[i]);\
    free(args)
#define forever while(1)
#define MAX(a,b) ((a>b)?a:b)

typedef long long muy_largo_t;
typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

/* Cooler write */
ssize_t better_write(int fd, const char *buf, size_t count) {
    size_t already_written = 0;
    ssize_t res_write;

    while (count > 0) {
        res_write = write(fd, buf + already_written, count);
        if (res_write <= 0) return res_write;
        already_written += res_write;
        count -= res_write;
    }
    return already_written;
}

/* Converts port name to a 16-bit unsigned integer */
static int convert_port_name(uint16_t *port, const char *port_name) {
    if (port_name == NULL || *port_name == '\0') return -1;

    char *end;
    muy_largo_t nn = strtoll(port_name, &end, 0);
    if (*end != '\0') return -1;
    if (nn < 0 || nn > 65535) return -1;

    *port = (uint16_t)nn;
    return 0;
}

int main(int argc, char *argv[]){
    /* Check args */
    if(argc < 2){
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    /* Convert port number */
    uint16_t port;
    if(convert_port_name(&port, argv[1]) < 0){
        PRINTERR("Invalid port number");
        return 1;
    }

    /* Open socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        PRINTERR("Socket creation failed");
        return 1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* Bind socket */
    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        PRINTERR("Bind failed");
        close(sockfd);
        return 1;
    }

    /* Listener */
    if(listen(sockfd, 1) < 0){
        PRINTERR("Listen failed");
        CLSOCK(sockfd);
        return 1;
    }

    /* Accept */
    sockaddr_in client_address;
    socklen_t clientaddr_len = sizeof(client_address);
    int client_socketfd;
    if((client_socketfd = accept(sockfd, (sockaddr *)&client_address, &clientaddr_len)) < 0){
        PRINTERR("Accept failed");
        CLSOCK(sockfd);
        return 1;
    }

    /* Receive number of arguments */
    uint16_t num_args;
    if(recv(client_socketfd, &num_args, sizeof(num_args), 0) < 0){
        PRINTERR("Receive failed");
        CLSOCK(client_socketfd);
        CLSOCK(sockfd);
        return 1;
    }
    num_args = ntohs(num_args);

    /* Allocate memory for arguments */
    char **args = calloc(num_args + 1, sizeof(char *));
    if(args == NULL){
        PRINTERR("Memory allocation failed");
        CLSOCK(client_socketfd);
        CLSOCK(sockfd);
        return 1;
    }

    args[num_args] = NULL;

    /* Read arguments */
    for (int i = 0; i < num_args; ++i) {
        uint16_t arg_len;
        if(recv(client_socketfd, &arg_len, sizeof(arg_len), 0) <= 0){
            PRINTERR("Receive failed");
            CLEANARGS;
            CLSOCK(client_socketfd);
            CLSOCK(sockfd);
            return 1;
        }
        arg_len = ntohs(arg_len);
        args[i] = calloc(arg_len + 1, 1);
        if(args[i] == NULL){
            PRINTERR("Memory allocation failed");
            CLEANARGS;
            CLSOCK(client_socketfd);
            CLSOCK(sockfd);
            return 1;
        }
        if(recv(client_socketfd, args[i], arg_len, 0) <= 0){
            PRINTERR("Receive failed");
            CLEANARGS;
            CLSOCK(client_socketfd);
            CLSOCK(sockfd);
            return 1;
        }
    }

    /* Set up pipes */
    int to_child[2], from_child[2];
    if(pipe(to_child) < 0 || pipe(from_child) < 0){
        PRINTERR("Pipe failed");
        CLEANARGS;
        CLSOCK(client_socketfd);
        CLSOCK(sockfd);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        PRINTERR("Fork failed");
        CLEANARGS;
        CLSOCK(client_socketfd);
        CLSOCK(sockfd);
        return 1;
    } else if (pid == 0) {
        /* Child process */
        CLS(to_child[1]);
        CLS(from_child[0]);

        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);

        /* Close unused descriptors */
        CLS(client_socketfd);
        CLS(sockfd);
        CLS(to_child[0]);
        CLS(from_child[1]);

        execvp(args[0], args);
        PRINTERR("execvp failed");
        exit(1);
    } else {
        /* Parent process */
        CLS(to_child[0]);
        CLS(from_child[1]);

        fd_set readfds;
        char buffer[1024];
        ssize_t nbytes;

        /* Relay data */
        forever {
            FD_ZERO(&readfds);
            FD_SET(client_socketfd, &readfds);
            FD_SET(from_child[0], &readfds);

            int max_fd = MAX(client_socketfd, from_child[0]) + 1;
            if(select(max_fd, &readfds, NULL, NULL, NULL) < 0){
                PRINTERR("Select failed");
                break;
            }

            if (FD_ISSET(client_socketfd, &readfds)) {
                if((nbytes = recv(client_socketfd, buffer, sizeof(buffer), 0)) <= 0) break;
                if(better_write(to_child[1], buffer, nbytes) <= 0) break;
            }

            if (FD_ISSET(from_child[0], &readfds)) {
                if((nbytes = read(from_child[0], buffer, sizeof(buffer))) <= 0) break;
                if(send(client_socketfd, buffer, nbytes, 0) <= 0) break;
            }
        }

        /* Clean up */
        CLEANARGS;
        CLSOCK(client_socketfd);
        CLSOCK(sockfd);
        CLS(to_child[1]);
        CLS(from_child[0]);
        waitpid(pid, NULL, 0);
    }

    return 0;
}#!/bin/bash
export address=129.108.156.68
export port=8080
commands=("ls -l" "pwd" "date" "lscpu" "cat")


for i in "${commands[@]}"; do
    echo -e "\033[1;92mRunning command: $i\033[0m\n"
    ./client $address $port "$i"
    pkill -f "./client $address $port"
    echo -e "\n"
    wait
done

