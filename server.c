#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048

int main(int argc, char **argv)
{
    if(argc != 2)
    {
		printf("Usage: %s <port>\n", argv[0]);
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1";
	int port = atoi(argv[1]);

	int option = 1;
	int listenfd = 0, connfd = 0;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    pthread_t tid;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    /* Signals */
	signal(SIGPIPE, SIG_IGN);

	if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR),(char*)&option,sizeof(option)) < 0)
    {
		perror("ERROR: setsockopt failed");
        return EXIT_FAILURE;
	}

	/* Bind */
    if(bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("ERROR: Socket binding failed");
        return EXIT_FAILURE;
    }

    /* Listen */
    if (listen(listenfd, 10) < 0)
    {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
	}

	printf("=== WELCOME TO THE CHATROOM ===\n");

	return EXIT_SUCCESS;
}
