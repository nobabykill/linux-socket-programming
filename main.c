#include "module/parse_url/url_parser.h"
#include <arpa/inet.h>
#include <libgen.h>
#include <linux/limits.h>
#include <linux/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFSZ 5000

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;
typedef struct hostent hostent;
typedef struct in_addr in_addr;

void error_handler(char *message);
void get_URL_info(sockaddr_in *addr, url_info *info);

int main(int argc, char const *argv[])
{
    if (argc < 2)
    {
        printf("missing URL parameter\n");
        return -1;
    }

    sockaddr_in *addr = (sockaddr_in *)malloc(sizeof(sockaddr_in));
    if (addr == NULL)
        error_handler("not enough memory space");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error_handler("create socket fd fail");

    // parse url field from argv
    url_info *info = (url_info *)malloc(sizeof(url_info));
    if (info == NULL)
        error_handler("not enough memory space");
    parse_url(info, argv[1]);

    printf("protocol: %s\n"
           "host: %s\n"
           "port: %d\n"
           "path: %s\n",
           info->protocol, info->host, info->port, info->path);

    // get data from URLinfo to sockaddr_in
    get_URL_info(addr, info);

    int ret = connect(sockfd, (sockaddr *)addr, INET_ADDRSTRLEN);
    if (ret < 0)
        error_handler("connect to server fail");

    char *buffer = (char *)malloc(BUFFSZ);
    memset(buffer, '\0', BUFFSZ);

    snprintf(
        buffer, BUFFSZ,
        "GET /%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        //"Accept: "
        //"text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        //"Accept-Language: en-US,en;q=0.5\r\n"
        //"Accept-Encoding: gzip, deflate\r\n"
        //"Connection: keep-alive\r\n"
        "\r\n",
        info->path, info->host);

    ret = send(sockfd, buffer, BUFFSZ, 0);
    if (ret < 0)
        error_handler("send data fail");
    memset(buffer, '\0', strlen(buffer));

    char *exec_dir = dirname(
        realpath(argv[0], NULL)); // get abs directory of executable file
    char save_path[PATH_MAX]; // use to save path of downloaded file (or dir)
    sprintf(save_path, "%s/%s.http", exec_dir, basename(info->path));

    FILE *fd_data = fopen(save_path, "wb");
    while ((ret = recv(sockfd, buffer, BUFFSZ, 0)) > 0)
    {
        printf("%d\n", ret);
        fwrite(buffer, 1, ret, fd_data);
        memset(buffer, '\0', ret);
    }

    // free data allocated
    free(info);
    free(addr);
    free(buffer);
    exit(0);
}

void error_handler(char *message)
{
    perror(message);
    exit(1);
}

void get_URL_info(sockaddr_in *addr, url_info *info)
{
    // declare addr data
    addr->sin_family = AF_INET;
    addr->sin_port = htons(info->port);
    hostent *he = gethostbyname(info->host);
    addr->sin_addr = *((in_addr *)he->h_addr_list[0]);
    bzero(&(addr->sin_zero), 8);
}