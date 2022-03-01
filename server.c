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

static _Atomic unsigned int client_count = 0;
static int uid = 10;

/* Client structure */
typedef struct{
	struct sockaddr_in address;
	int sockfd;
	int uid;
	char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout()
{
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf (char* arr, int length)
{
    int i;
    for (i = 0; i < length; i++)
    { // trim \n
        if (arr[i] == '\n')
        {
            arr[i] = '\0';
            break;
        }
    }
}

/* Add clients to queue */
void queue_add(client_t *cl)
{
	pthread_mutex_lock(&clients_mutex);
	for(int i=0; i < MAX_CLIENTS; i++)
    {
		if(!clients[i])
		{
			clients[i] = cl;
			break;
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients from queue */
void queue_remove(int uid)
{
	pthread_mutex_lock(&clients_mutex);
	for(int i=0; i < MAX_CLIENTS; i++)
    {
		if(clients[i])
		{
			if(clients[i]->uid == uid)
			{
				clients[i] = NULL;
				break;
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

void print_ip_addr(struct sockaddr_in addr)
{
    printf("%d.%d.%d.%d",
         addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Send message to all clients except sender */
void send_message(char *message, int uid)
{
	pthread_mutex_lock(&clients_mutex);
	for(int i=0; i<MAX_CLIENTS; i++)
    {
		if(clients[i])
        {
			if(clients[i]->uid != uid)
			{
				if(write(clients[i]->sockfd, message, strlen(message)) < 0)
				{
					perror("ERROR: write to descriptor failed");
					break;
				}
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

/* Handle all communication with the client */
void *handle_client(void *arg)
{
    char buff_out[BUFFER_SZ];
	char name[32];
	int leave_flag = 0;

	client_count++;
	client_t *client = (client_t *)arg;

	// Name
	if(recv(client->sockfd, name, 32, 0) <= 0 || strlen(name) <  2 || strlen(name) >= 32-1)
    {
		printf("Didn't enter the name.\n");
		leave_flag = 1;
	}
	else
	{
		strcpy(client->name, name);
		sprintf(buff_out, "%s has joined\n", client->name);
		printf("%s", buff_out);
		send_message(buff_out, client->uid);
	}

	bzero(buff_out, BUFFER_SZ);

	while(1)
    {
        if (leave_flag)
        {
			break;
		}
        int receive = recv(client->sockfd, buff_out, BUFFER_SZ, 0);
		if (receive > 0)
        {
			if(strlen(buff_out) > 0)
			{
				send_message(buff_out, client->uid);
				str_trim_lf(buff_out, strlen(buff_out));
				printf("%s -> %s\n", buff_out, client->name);
			}
		}
		else if (receive == 0 || strcmp(buff_out, "exit") == 0)
        {
			sprintf(buff_out, "%s has left\n", client->name);
			printf("%s", buff_out);
			send_message(buff_out, client->uid);
			leave_flag = 1;
		}
		else
        {
			printf("ERROR: -1\n");
			leave_flag = 1;
		}
		bzero(buff_out, BUFFER_SZ);
    }

    /* Delete client from queue and yield thread */
	close(client->sockfd);
    queue_remove(client->uid);
    free(client);
    client_count--;
    pthread_detach(pthread_self());

	return NULL;
}

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
    pthread_t thread_id;

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

	while(1)
    {
        socklen_t client_len = sizeof(client_addr);
		connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_len);

		/* Check if max clients is reached */
		if((client_count + 1) == MAX_CLIENTS)
        {
			printf("Maximum clients reached. Rejected: ");
			print_ip_addr(client_addr);
			close(connfd);
			continue;
		}

        /* Client settings */
		client_t *client = (client_t *)malloc(sizeof(client_t));
		client->address = client_addr;
		client->sockfd = connfd;
		client->uid = uid++;

		/* Add client to the queue and fork thread */
		queue_add(client);
		pthread_create(&thread_id, NULL, &handle_client, (void*)client);

		/* Reduce CPU usage */
		sleep(1);
    }
	return EXIT_SUCCESS;
}
