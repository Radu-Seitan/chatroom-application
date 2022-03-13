# chatroom-application

## Compilation
In order to compile the C files, use the following commands:  
`gcc -Wall -g3 -fsanitize=address -pthread server.c -o server`  
`gcc -Wall -g3 -fsanitize=address -pthread client.c -o client`
