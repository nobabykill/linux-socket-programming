#include <ctype.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_PORT 44444
#define MSG_SIZE 100
#define PROJ_ID 69

// structure for message queue
struct mesg_buffer
{
    long msg_type;
    char msg_text[MSG_SIZE];
} message;

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

int main()
{
    int ret;
    int srvsock, clisock;
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[MSG_SIZE];
    key_t key;
    int msgid;
    // ftok to generate unique key
    key = ftok("./", PROJ_ID);

    // msgget creates a message queue
    // and returns identifier
    msgid = msgget(key, 0666 | IPC_CREAT);

    message.msg_type = 1;

    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_family = AF_INET;

    if ((srvsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("create socket fail");
        exit(EXIT_FAILURE);
    }
    setsockopt(srvsock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    if (bind(srvsock, (sockaddr *)&addr, addrlen) < 0)
    {
        perror("bind socket fail");
        exit(EXIT_FAILURE);
    }

    if (listen(srvsock, 0) < 0)
    {
        perror("listen fail");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        if ((clisock = accept(srvsock, (sockaddr *)&addr, &addrlen)) < 0)
            break;

        if ((ret = recv(clisock, buffer, MSG_SIZE, 0)) < 0)
        {
            close(clisock);
            continue;
        }
        buffer[ret] = '\0';

        printf("new client connected: %s\n", buffer);
        strcpy(message.msg_text, buffer);
        // msgsnd to send message
        msgsnd(msgid, &message, sizeof(message), 0);

        // upper case all message then send back to client
        for (int i = 0; i < ret; i++)
            buffer[i] = toupper(buffer[i]);

        if (send(clisock, buffer, ret, 0) < 0)
        {
            close(clisock);
            continue;
        }

        close(clisock);
    }

    close(srvsock);
    exit(EXIT_SUCCESS);
}
