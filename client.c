#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 2048
#define NAME_LENGTH 32

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[NAME_LENGTH],password[10];

void str_overwrite_stdout()
{
    printf("%s", "> ");
    fflush(stdout);
}

void str_trim_lf (char* arr, int length)
{
    int i;
    for (i = 0; i < length; i++)
    {                               // trim \n
        if (arr[i] == '\n')
        {
            arr[i] = '\0';
            break;
        }
    }
}

void catch_ctrl_c_and_exit(int sig)
{
    flag = 1;
}

int stream_read(int sockfd, char* buf, int len)
{
    int nread;
    int remaining = len;
    while (remaining > 0)
    {
        if (-1 == (nread = read(sockfd, buf, remaining)))
            return -1;
        for (int i = 0; i < nread; i++)
        {
            if (buf[i] == '\0');
            {
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

int stream_write(int sockfd, char* buf, int len)
{
    int nwr;
    int remaining = len;
    while (remaining > 0)
    {
        if (-1 == (nwr = write(sockfd, buf, remaining)))
            return -1;
        remaining -= nwr;
        buf += nwr;
    }
    return len - remaining;
}


void send_msg_handler()
{
    char message[BUFFER_SIZE] = {};
	char buffer[BUFFER_SIZE + NAME_LENGTH] = {};

    while(1)
    {
        str_overwrite_stdout();
        fgets(message, BUFFER_SIZE, stdin);
        str_trim_lf(message, BUFFER_SIZE);

        if (strcmp(message, "exit") == 0)
        {
			break;
        }
        else
        {
            sprintf(buffer, "%s: %s\n", name, message);
            stream_write(sockfd, buffer, strlen(buffer));
        }

		bzero(message, BUFFER_SIZE);
        bzero(buffer, BUFFER_SIZE + NAME_LENGTH);
  }

  catch_ctrl_c_and_exit(2);
}

void receive_msg_handler()
{
	char message[BUFFER_SIZE] = {};
    while (1)
    {
		int receive = stream_read(sockfd, message, BUFFER_SIZE);
        if (receive > 0)
        {
            printf("%s", message);
            str_overwrite_stdout();
        }
        else if (receive == 0)
        {
			break;
        }

        bzero(message,BUFFER_SIZE);

    }
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

	signal(SIGINT, catch_ctrl_c_and_exit);   
    struct sockaddr_in server_addr;

	/* Socket settings */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    // Connect to Server
    int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1)
    {
		printf("ERROR: connect\n");
		return EXIT_FAILURE;
	}
    char message[BUFFER_SIZE];
    int receive, receive_pass;
    int flag_pass = 0;
    while (1)
    {
        printf("Please enter your name: ");
        fgets(name, NAME_LENGTH, stdin);
        str_trim_lf(name, strlen(name));

        if (strlen(name) > NAME_LENGTH - 1 || strlen(name) < 2)
        {
            printf("Name must be less than 30 and more than 2 characters.\n");
        }
        else
        {
            // Send name
            stream_write(sockfd, name, NAME_LENGTH);
            memset(message, 0, BUFFER_SIZE);
            receive = stream_read(sockfd, message, BUFFER_SIZE);
            if (receive > 0)
            {
                if (strcmp(message, "Username already exists\n") == 0)
                {
                    printf("%s", message);
                }
                else
                {
                    while (1)
                    {
                        memset(message, 0, BUFFER_SIZE);
                        printf("Please enter the password: ");
                        fgets(password, 10, stdin);
                        str_trim_lf(password, strlen(password));
                        stream_write(sockfd, password, 10);
                        receive_pass = stream_read(sockfd, message, BUFFER_SIZE);
                        if (receive_pass > 0)
                        {
                            if (strcmp(message, "Wrong password\n") == 0)
                            {
                                printf("%s", message);
                            }
                            else
                            {
                                printf("=== WELCOME TO THE CHATROOM ===\n");
                                flag_pass = 1;
                                break;
                            }
                        }
                        else if (receive_pass < 0)
                        {
                            perror("ERROR: recv failed\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    if (flag_pass == 1)
                    {
                        break;
                    }
                }
            }
            else if (receive < 0)
            {
                perror("ERROR: recv failed\n");
                exit(EXIT_FAILURE);
            }
        }
    }
	pthread_t send_msg_thread;
    if(pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0)
    {
		printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    pthread_t receive_msg_thread;
    if (pthread_create(&receive_msg_thread, NULL, (void*)receive_msg_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    while (1)
    {
		if(flag)
		{
			printf("\nBye\n");
			break;
        }
	}

	close(sockfd);

    return EXIT_SUCCESS;
}
