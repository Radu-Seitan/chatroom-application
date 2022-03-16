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
#define BUFFER_SIZE 2048

static _Atomic unsigned int client_count = 0;
char chat_password[10] = "1234";

/* Client structure */
typedef struct {
	struct sockaddr_in address;
	int sockfd;
	char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_trim_lf (char* arr, int length) {
    for (int i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n')
        {
            arr[i] = '\0';
            break;
        }
    }
}

/* Add clients to queue */
void queue_add(client_t *cl) {
	if(pthread_mutex_lock(&clients_mutex) != 0 ) {
        fprintf(stderr, "ERROR: Phtread mutex lock failed");
        exit(EXIT_FAILURE);
    }
	for(int i = 0; i < MAX_CLIENTS; i++) {
		if(!clients[i])
		{
			clients[i] = cl;
			break;
		}
	}
	if(pthread_mutex_unlock(&clients_mutex) != 0) {
        fprintf(stderr, "ERROR: Phtread mutex unlock failed");
        exit(EXIT_FAILURE);
    }
}

/* Remove clients from queue */
void queue_remove(char* name) {
	if(pthread_mutex_lock(&clients_mutex) != 0 ) {
        fprintf(stderr, "ERROR: Phtread mutex lock failed");
        exit(EXIT_FAILURE);
    }
	for(int i = 0; i < MAX_CLIENTS; i++) {
		if(clients[i]) {
			if(strcmp(clients[i]->name, name) == 0) {
				clients[i] = NULL;
				break;
			}
		}
	}
	if(pthread_mutex_unlock(&clients_mutex) != 0) {
        fprintf(stderr, "ERROR: Phtread mutex unlock failed");
        exit(EXIT_FAILURE);
    }
}

void print_ip_addr(struct sockaddr_in addr) {
    printf("%d.%d.%d.%d",
         addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

int stream_read(int sockfd, char *buf, int len) {
    int nread;
    int remaining = len;
    while (remaining > 0) {
        if (-1 == (nread = read(sockfd, buf, remaining)))
            return -1;
        for (int i = 0; i < nread; i++) {
            if (buf[i] == '\0')
			; {
                return 1;
            }
        }
        if (nread == 0) 
            break;
        remaining -= nread;
        buf += nread;
    }
    return len - remaining;
}

int stream_write(int sockfd, char *buf, int len) {
    int nwr;
    int remaining = len;
    while (remaining > 0) {
        if (-1 == (nwr = write(sockfd, buf, remaining)))
            return -1;
        remaining -= nwr;
        buf += nwr;
    }
    return len - remaining;
}

/* Send message to all clients except sender */
void send_message(char *message, char* name) {
	if(pthread_mutex_lock(&clients_mutex) != 0 ) {
        fprintf(stderr, "ERROR: Phtread mutex lock failed");
        exit(EXIT_FAILURE);
    }
	for(int i = 0; i < MAX_CLIENTS; i++) {
		if(clients[i]) {
			if(strcmp(clients[i]->name, name) != 0) {
                if(stream_write(clients[i]->sockfd, message, strlen(message)) < 0) {
					perror("ERROR: write to descriptor failed");
					break;
				}
			}
		}
	}
	if(pthread_mutex_unlock(&clients_mutex) != 0) {
        fprintf(stderr, "ERROR: Phtread mutex unlock failed");
        exit(EXIT_FAILURE);
    }
}

int check_username_already_exists(char *username) {
    if(pthread_mutex_lock(&clients_mutex) != 0 ) {
        fprintf(stderr, "ERROR: Phtread mutex lock failed");
        exit(EXIT_FAILURE);
    }
    int exists_flag = 0;
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i]) {
            if(strcmp(clients[i]->name,username)==0)
            {
                exists_flag = 1;
                break;
            }
        }
    }
    if(pthread_mutex_unlock(&clients_mutex) != 0) {
        fprintf(stderr, "ERROR: Phtread mutex unlock failed");
        exit(EXIT_FAILURE);
    }
    return exists_flag;
}

/* Handle all communication with the client */
void *handle_client(void *arg)
{
    char buff_out[BUFFER_SIZE] = {};
	int leave_flag = 0;
	client_t *client = (client_t *)arg;
  
	while(1) {
        if (leave_flag)
			break;
        int receive = stream_read(client->sockfd, buff_out, BUFFER_SIZE);
		if (receive > 0){
			if(strlen(buff_out) > 0){
				send_message(buff_out, client->name);
				str_trim_lf(buff_out, strlen(buff_out));
				printf("%s\n", buff_out);
			}
		}
		else if (receive == 0 || strcmp(buff_out, "exit") == 0) {
			sprintf(buff_out, "%s has left\n", client->name);
			printf("%s", buff_out);
			send_message(buff_out, client->name);
			leave_flag = 1;
		}
		else {
			perror("ERROR: read failed\n");
			leave_flag = 1;
		}
		bzero(buff_out, BUFFER_SIZE);
    }

    /* Delete client from queue and yield thread */
	if(close(client->sockfd) < 0) {
		perror("ERROR: Close client sockfd failed");
		exit(EXIT_FAILURE);
	}
	queue_remove(client->name);
	free(client);
	client_count--;
	if(pthread_detach(pthread_self()) != 0) {
		fprintf(stderr, "ERROR: Phtread detach failed");
		exit(EXIT_FAILURE);
	}
	return NULL;
}

int main(int argc, char **argv) {
    if(argc != 2) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	char *ip = "0.0.0.0";
	int port = atoi(argv[1]);

	int option = 1;
	int listenfd = 0, connfd = 0;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    pthread_t thread_id;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0) {
        perror("ERROR: Socket failed");
        exit(EXIT_FAILURE);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    /* Signals */
    struct sigaction action_ignore;
    memset(&action_ignore, 0x00, sizeof(struct sigaction));
    action_ignore.sa_handler = SIG_IGN;
    if(sigaction(SIGPIPE, &action_ignore, NULL) < 0) {
        perror("ERROR: Sigaction failed");
        exit(EXIT_FAILURE);
    }

	if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEADDR),(char*)&option,sizeof(option)) < 0) {
		perror("ERROR: Setsockopt failed");
        exit(EXIT_FAILURE);
	}

	/* Bind */
    if(bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("ERROR: Socket binding failed");
        exit(EXIT_FAILURE);
    }

    /* Listen */
    if (listen(listenfd, 10) < 0) {
        perror("ERROR: Socket listening failed");
        exit(EXIT_FAILURE);
	}

	printf("=== WELCOME TO THE CHATROOM ===\n");

	while(1) {
        socklen_t client_len = sizeof(client_addr);
		connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_len);
		if(connfd < 0) {
            perror("ERROR: Accept failed");
            exit(EXIT_FAILURE);
        }

		/* Check if max clients is reached */
		if((client_count + 1) == MAX_CLIENTS) {
			printf("Maximum clients reached. Rejected: ");
			print_ip_addr(client_addr);
			if(close(connfd) < 0) {
				perror("ERROR: Failed to close file descriptor");
				exit(EXIT_FAILURE);
			}
			continue;
		}
        
        /* Client settings */
		client_t *client = (client_t *)malloc(sizeof(client_t));
        if(client == NULL) {
            perror("ERROR: Memory not allocated");
            exit(EXIT_FAILURE);
        }
		client->address = client_addr;
		client->sockfd = connfd;
        
        char buff_out[BUFFER_SIZE];
        char name[32], password[10];
        int receive;
        while (1) {
            if (stream_read(client->sockfd, name, 32) < 0) {
                perror("ERROR: read failed\n");
                exit(EXIT_FAILURE);
            }
           
            else if (strlen(name) > 32 - 1 || strlen(name) < 2) {
                printf("Name must be less than 30 and more than 2 characters.\n");
            }
            else {
                memset(buff_out, 0, BUFFER_SIZE);
                if (check_username_already_exists(name) == 0) {
                    strcpy(client->name, name);
                    sprintf(buff_out, "Username does not exist\n");
                    if (stream_write(client->sockfd, buff_out, strlen(buff_out)) < 0) {
                        perror("ERROR: write to descriptor failed");
                        exit(EXIT_FAILURE);
                    }
                    while (1) {
                        if ((receive = stream_read(client->sockfd, password, 10)) > 0) {
                            memset(buff_out, 0, BUFFER_SIZE);
                            if (strcmp(password, chat_password) == 0) {
                                sprintf(buff_out, "%s has joined\n", client->name);
                                printf("%s", buff_out);
                                send_message(buff_out, client->name);
                                sprintf(buff_out, "Password correct\n");
                                if (stream_write(client->sockfd, buff_out, strlen(buff_out)) < 0) {
                                    perror("ERROR: write to descriptor failed");
                                    exit(EXIT_FAILURE);
                                }

                                queue_add(client);
                                client_count++;
                                if (pthread_create(&thread_id, NULL, &handle_client, (void*)client) != 0) {
                                    fprintf(stderr, "ERROR: Pthread create failed");
                                    exit(EXIT_FAILURE);
                                }
                                break;
                            }
                            else {
                                sprintf(buff_out, "Wrong password\n");
                                if (stream_write(client->sockfd, buff_out, strlen(buff_out)) < 0) {
                                    perror("ERROR: write to descriptor failed");
                                    exit(EXIT_FAILURE);
                                }
                            }
                        }
                        else if (receive < 0) {
                            perror("ERROR: read failed\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    break;
                } else {
                    sprintf(buff_out, "Username already exists\n");
                    if (stream_write(client->sockfd, buff_out, strlen(buff_out)) < 0) {
                        perror("ERROR: write to descriptor failed");
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }

		sleep(1);
    }
	exit(EXIT_SUCCESS);
}
