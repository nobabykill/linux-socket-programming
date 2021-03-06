#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define BUFFSIZE 512
#define HEADER_SIZE 4000
#define MAX_POLL_FD 100   // 100 fd list
#define POLL_TIMEOUT 1000 // 1000 millisec
#define HTTP_TIMEOUT 3    // http timeout 3s
#define USAGE "Usage: webserver [-p <PORT>] [-d <RESOURCE_DIR>]\n"
#define VERSION                                                                                    \
    "webserver v1.0.0\n"                                                                           \
    "Written by Linh, Khue, Tin\n"
#define HELP                                                                                       \
    "webserver [-p <PORT>] [-d <RESOURCE_DIR>]\n"                                                  \
    "Serve GET request from client\n"                                                              \
    "\n"                                                                                           \
    "-p, --port         listening port of webserver server (default 11111)\n"                      \
    "-d, --dir          resource dir (default ./)\n"                                               \
    "-h, --help         display help message then exit\n"                                          \
    "-v, --version      output version information then exit\n"                                    \
    "\n"

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;
typedef struct timeval timeval;
typedef struct pollfd pollfd;

static int is_dir(char *path);
static void init_server_socket(int *sockfd, sockaddr_in *servaddr);
static int get_req_headers(char *hbuff, int sockfd);
static char *concat(const char *s1, const char *s2);
static unsigned long get_file_size(char *path);
static int serve_client_request(int sockfd);
static void remove_fd(pollfd *ufds, int i);

static struct option long_options[] = {{"port", required_argument, 0, 'p'},
                                       {"dir", required_argument, 0, 'd'},
                                       {"help", no_argument, 0, 'h'},
                                       {"version", no_argument, 0, 'v'},
                                       {0, 0, 0, 0}};

static int port = 11111; // default webserver port
static char *dir = NULL; // default resource dir

int main(int argc, char *argv[])
{
    int c;
    int option_index;

    while (1)
    {
        c = getopt_long(argc, argv, "p:d:hv", long_options, &option_index);
        if (c == -1) // end of options
            break;
        switch (c)
        {
        case 'p':
            port = (int)strtol(optarg, NULL, 10);
            break;
        case 'd':
            dir = optarg;
            if (is_dir(dir) == 0) // invalid dir
            {
                printf("%s is invalid dir\n", dir);
                exit(EXIT_FAILURE);
            }
            if (dir[strlen(dir) - 1] == '/') // strip off '/' character if exist
                dir[strlen(dir) - 1] = '\0';
            break;
        case 'h':
            printf(HELP);
            exit(EXIT_SUCCESS);
        case 'v':
            printf(VERSION);
            exit(EXIT_SUCCESS);
        case '?':
            printf("Try 'client --help' for more information.\n");
            exit(EXIT_FAILURE);
        default:
            abort();
        }
    }

    char abs_dir[PATH_MAX];
    if (dir == NULL || (dir = realpath(dir, abs_dir)) == NULL)
    {
        printf("Missing resource dir or dir not exist.\nTry 'webserver --help' "
               "for more "
               "information.\n");
        exit(EXIT_FAILURE);
    }

    // init server socket
    int sockfd;
    sockaddr_in servaddr;
    pollfd ufds[MAX_POLL_FD];        // use to poll event
    time_t elapse_time[MAX_POLL_FD]; // use to track inactive fd

    for (int i = 0; i < MAX_POLL_FD; i++)
    {
        ufds[i].fd = -1;
        ufds[i].events = POLLIN;
    }
    init_server_socket(&sockfd, &servaddr);
    int UPDATE_POLLFD = 0; // flag to decide update list pollfd or not
    ufds[0].fd = sockfd;
    int nfds = 1;

    while (1)
    {
        int ret = poll(ufds, nfds, POLL_TIMEOUT);

        if (ret == 0) // timeout, check timestamp each connection
        {
            time_t now = time(NULL);
            for (int i = 1; i < nfds; i++)
            {
                int second_gap = (int)(now - elapse_time[i]);
                if (second_gap > HTTP_TIMEOUT)
                {
                    close(ufds[i].fd);
                    printf("closed connection (%d)\n", ufds[i].fd);
                    ufds[i].fd = -1;
                    nfds--;
                    UPDATE_POLLFD = 1;
                }
            }
        }
        else if (ret > 0) // poll success
        {
            if (ufds[0].revents == POLLIN) // new client connect
            {
                int newsock = accept(sockfd, NULL, NULL); // accept new connection
                if (newsock > 0)                          // accept success
                {
                    ufds[nfds].fd = newsock;
                    ufds[nfds].events = POLLIN;
                    time(&elapse_time[nfds]);
                    nfds++; // update max index of pollfd
                }
                else
                    perror("accept connection fail");

                continue; // repoll
            }

            int count = 0;
            for (int i = 1; i < nfds; i++)
            {
                if (count == ret) // processed all event
                    break;

                if (ufds[i].revents != 0) // new event occur
                {
                    int code = ufds[i].revents;

                    if (code == POLLIN) // process incoming data
                    {
                        if (serve_client_request(ufds[i].fd) != 1)
                        { // some error when reading fd or invalid url
                            remove_fd(ufds, i);
                            nfds--;
                            UPDATE_POLLFD = 1;
                        }
                    }
                    else // some error happen, close socket, update pollfd list
                    {
                        remove_fd(ufds, i);
                        nfds--;
                        UPDATE_POLLFD = 1;
                    }

                    count++;
                }
            }
        }
        else // poll fail
        {
            printf("poll failed, exit\n");
            exit(EXIT_FAILURE);
        }

        if (UPDATE_POLLFD == 1) // update pollfd list and time lapse list
        {
            for (int i = 1; i < nfds; i++)
                if (ufds[i].fd == -1) // empty fd
                    for (int j = i + 1; j < MAX_POLL_FD; j++)
                        if (ufds[j].fd != -1) // replace with previous empty fd
                        {
                            ufds[i].fd = ufds[j].fd;
                            elapse_time[i] = elapse_time[j];
                            ufds[j].fd = -1;
                            break;
                        }

            UPDATE_POLLFD = 0;
        }
    }

    exit(EXIT_SUCCESS);
}

int is_dir(char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

void init_server_socket(int *sockfd, sockaddr_in *servaddr)
{
    // assign ip, port
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr->sin_port = htons(port);
    servaddr->sin_family = AF_INET;

    // create new socket
    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("create socket fail");
        exit(EXIT_FAILURE);
    }
    setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    fcntl(*sockfd, F_SETFL, O_NONBLOCK);

    // Binding newly created socket to given IP and verification
    if ((bind(*sockfd, (sockaddr *)servaddr, sizeof(*servaddr))) != 0)
    {
        perror("socket bind failed...");
        exit(EXIT_FAILURE);
    }

    // Now server is ready to listen and verification
    if ((listen(*sockfd, 0)) != 0)
    {
        perror("Listen failed...\n");
        exit(EXIT_FAILURE);
    }
}

int get_req_headers(char *hbuff, int sockfd)
{
    int ret;
    char c;
    char *ptr = hbuff;

    while ((ret = recv(sockfd, &c, 1, 0)) > 0)
    {
        *ptr = c;
        ptr++;
        if (ptr[-1] == '\n' && ptr[-2] == '\r' && ptr[-3] == '\n' && ptr[-4] == '\r')
            break;
    }
    return ret; // ret <= 0 (error or connection closed)
}

char *concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator
    // in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

unsigned long get_file_size(char *path)
{
    struct stat st;
    stat(path, &st);
    return st.st_size;
}

int serve_client_request(int sockfd)
{
    char buffer[BUFFSIZE];
    memset(buffer, '\0', BUFFSIZE);
    char header[HEADER_SIZE];
    int ret;
    memset(header, '\0', HEADER_SIZE);
    char req_file[PATH_MAX];
    char method[10];

    ret = get_req_headers(header, sockfd); // get request header
    if (ret <= 0)                          // error when reading header
        return ret;

    sscanf(header, "%s %s %*s", method, req_file);
    if (strcmp(method, "GET") != 0)
    {
        printf("%s\n",method);
        close(sockfd);
        return -1;
    }
    printf("GET %s (%d)\n", req_file, sockfd);
    if (strcmp(req_file, "/") == 0)
        strcpy(req_file, "/index.html");

    char *path_file = concat(dir, req_file);
    FILE *f = fopen(path_file, "rb");

    if (f != NULL) // file exist
    {
        char *success_res_header = "HTTP/1.1 200 OK\r\n"
                                   "Connection: keep-alive\r\n"
                                   "Keep-Alive: timeout=5\r\n";
        send(sockfd, success_res_header, strlen(success_res_header), 0);

        sprintf(buffer, "Content-Length: %lu\r\n\r\n", get_file_size(path_file));
        send(sockfd, buffer, strlen(buffer), 0);

        memset(buffer, '\0', BUFFSIZE);
        while ((ret = fread(buffer, 1, BUFFSIZE - 1, f)) > 0)
        {
            if (send(sockfd, buffer, ret, 0) <= 0)
                break;
            memset(buffer, '\0', BUFFSIZE);
        }
        fclose(f);
    }
    else // file not found and seq == 0 (first GET request)
    {    // redirect 404 error
        char *redirect_404 = "HTTP/1.1 301 Moved Permanently\r\n"
                             "Location: /404.html\r\n"
                             "Connection: close\r\n\r\n";
        send(sockfd, redirect_404, strlen(redirect_404), 0);
        return 2;
    }

    free(path_file);
    return 1;
}

void remove_fd(pollfd *ufds, int i)
{
    close(ufds[i].fd);
    printf("closed connection (%d)\n", ufds[i].fd);
    ufds[i].fd = -1;
}