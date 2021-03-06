#include "module/parse_response_header/header_res_parser.h"
#include "module/parse_url/url_parser.h"
#include <arpa/inet.h>
#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <linux/socket.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFFSZ 5000
#define EXEC "1612340"
#define KILOBYTE 1024
#define MEGABYTE 1024 * KILOBYTE
#define GIGABYTE 1024 * MEGABYTE

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;
typedef struct hostent hostent;
typedef struct in_addr in_addr;

void error_handler(char *message);
void get_URL_info(sockaddr_in *addr, url_info *info);
int check_is_file(char *path);
void ParseHref(char *f1, char *f2);
int download_file(sockaddr_in *addr, char *save_path, url_info *info);
int download_dir(sockaddr_in *addr, char *save_dir, url_info *info);
int progress_bar(char *name, unsigned long cbyte, unsigned long totalbyte,
                 int time);
void clean_progressbar(int pchar);

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

    // parse url field from argv
    url_info *info = (url_info *)malloc(sizeof(url_info));
    if (info == NULL)
        error_handler("not enough memory space");
    parse_url(info, argv[1]);

    // get data from URLinfo to sockaddr_in
    get_URL_info(addr, info);

    char *exec_dir = dirname(
        realpath(argv[0], NULL)); // get abs directory of executable file

    if (check_is_file(info->path))
    {
        // get path same level with executable file to save
        char save_path[PATH_MAX]; // use this path to save downloaded file
        char *filename = basename(strcpy((char *)malloc(PATH_MAX), info->path));
        sprintf(save_path, "%s/%s_%s", exec_dir, EXEC, filename);
        download_file(addr, save_path, info);
    }
    else
        download_dir(addr, exec_dir, info);

    // free data allocated
    free(info);
    free(addr);
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

int check_is_file(char *path)
{
    char *ptr = path;
    while (*(ptr + 1) != '\0')
        ptr++;

    if (*ptr != '/')
        return 1;
    return 0;
}

void ParseHref(char *f1, char *f2)
{
    FILE *fsource = fopen(f1, "r");
    FILE *fdes = fopen(f2, "w");

    char buff[1000];
    char file[100];
    char *pos;
    memset(buff, '\0', 1000);

    while (fgets(buff, 1000, fsource))
    {
        pos = buff;
        do
        {
            pos = strstr(pos, "href");
            if (pos == NULL)
                break;

            sscanf(pos, "href=\"%99[^\"]\"%*s", file);

            fputs(file, fdes);
            fputs("\n", fdes);
            memset(file, '\0', strlen(file));

            pos++;

        } while (pos != NULL);
        memset(buff, '\0', strlen(buff));
    }

    fclose(fsource);
    fclose(fdes);
}

int download_file(sockaddr_in *addr, char *save_path, url_info *info)
{
    int ret;
    char buffer[BUFFSZ];
    memset(buffer, '\0', BUFFSZ);
    char *filename = basename(strcpy((char *)malloc(PATH_MAX), info->path));

    // send request headers to server
    snprintf(buffer, BUFFSZ,
             "GET /%s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "\r\n",
             info->path, info->host);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error_handler("create socket fd fail");

    // connect to remote server
    ret = connect(sockfd, (sockaddr *)addr, INET_ADDRSTRLEN);
    if (ret < 0)
        error_handler("connect to server fail");

    // send request to server
    ret = send(sockfd, buffer, strlen(buffer), 0);
    if (ret < 0)
        error_handler("send data fail");
    memset(buffer, '\0', strlen(buffer));

    // get hhtp headers
    char hbuff[HEADER_SIZE];
    memset(hbuff, '\0', HEADER_SIZE);
    get_headers(hbuff, sockfd);

    if (strlen(hbuff) == 0)
    {
        printf("no header response\n");
        return -1;
    }

    // status code wrong
    if ((ret = get_status_code(hbuff)) != 200)
    {
        printf("status code is %d\n", ret);
        return -1;
    }

    // get length of file
    char value[100];
    ret = search_header_value(value, "Content-Length", hbuff);
    if (ret < 0)
        return -1; // not file

    // get body data
    unsigned long flen = atoi(value);
    unsigned long bytes_recv = 0;
    FILE *fn = fopen(save_path, "wb");

    time_t start, end;
    time(&start);
    int printed_char = 0;
    while ((ret = recv(sockfd, buffer, BUFFSZ, 0)) > 0)
    {
        if (ret < 0)
            printf("%s", strerror(errno));

        fwrite(buffer, 1, ret, fn);
        memset(buffer, '\0', ret);

        bytes_recv += ret;
        time(&end);
        clean_progressbar(printed_char);
        printed_char =
            progress_bar(filename, bytes_recv, flen, difftime(end, start));
    }
    printf("\n");

    fclose(fn);
    close(sockfd);
    return 0;
}

int download_dir(sockaddr_in *addr, char *save_dir, url_info *info)
{
    int ret;
    char buffer[BUFFSZ];
    memset(buffer, '\0', BUFFSZ);

    // get path same level with executable file to save
    char save_path[PATH_MAX]; // use this path to save downloaded dir
    char *dirname = basename(strcpy((char *)malloc(PATH_MAX), info->path));
    sprintf(save_path, "%s/%s_%s/", save_dir, EXEC, dirname);

    // create directory
    ret = mkdir(save_path, 0755);

    // send request headers to server
    snprintf(buffer, BUFFSZ,
             "GET /%s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "\r\n",
             info->path, info->host);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error_handler("create socket fd fail");

    // connect to remote server
    ret = connect(sockfd, (sockaddr *)addr, INET_ADDRSTRLEN);
    if (ret < 0)
        error_handler("connect to server fail");

    // send request to server
    ret = send(sockfd, buffer, strlen(buffer), 0);
    if (ret < 0)
        error_handler("send data fail");
    memset(buffer, '\0', strlen(buffer));

    // get http headers
    char hbuff[HEADER_SIZE];
    memset(hbuff, '\0', HEADER_SIZE);
    get_headers(hbuff, sockfd);

    // status code wrong
    if ((ret = get_status_code(hbuff)) != 200)
    {
        printf("status code is %d\n", ret);
        return -1;
    }

    // get html body to fsource
    char f1[PATH_MAX]; // f1 store html body
    sprintf(f1, "%s/body.html", save_dir);
    char f2[PATH_MAX]; // f2 store file name (href) parse from f1
    sprintf(f2, "%s/link.txt", save_dir);

    FILE *fsource = fopen(f1, "wb");
    while ((ret = recv(sockfd, buffer, BUFFSZ, 0)) > 0)
        fwrite(buffer, 1, ret, fsource);
    close(sockfd); // close socket
    fclose(fsource);

    // parse href from fsource to fdes
    ParseHref(f1, f2);

    // get filename from link.txt and download
    FILE *fdes = fopen(f2, "r");
    char name[100];
    char *pos;
    while (fgets(name, sizeof(name), fdes))
    {
        // check name is filename or not
        if (strstr(name, "/") || strstr(name, "#") || strstr(name, "?"))
            continue; // not filename

        // remove trailing \n character
        if ((pos = strchr(name, '\n')) != NULL)
            *pos = '\0';

        url_info newinfo;
        memcpy((char *)&newinfo, (char *)info, sizeof(newinfo));
        strcat(newinfo.path, name); // append name to path of url
        // download each file
        char filepath[PATH_MAX];
        strcpy(filepath, save_path);
        download_file(addr, strcat(filepath, name), &newinfo);
    }
    fclose(fdes);
    // remove file
    remove(f1);
    remove(f2);
    return 0;
}

int progress_bar(char *name, unsigned long cbyte, unsigned long totalbyte,
                 int time)
{
    int barlen = 20;
    int max_name_len = 50;
    int printed = 0;

    // print filename
    printed += printf("%s", name);
    if (strlen(name) < max_name_len)
    {
        for (int i = 0; i < max_name_len - strlen(name); i++)
            printed += printf(" ");
    }

    // print total size
    if (cbyte < KILOBYTE)
        printed += printf("  %10.2f B", cbyte * 1.0);
    else if (cbyte >= KILOBYTE && cbyte < MEGABYTE)
        printed += printf("%10.2f KiB", cbyte * 1.0 / (KILOBYTE));
    else if (cbyte >= MEGABYTE && cbyte < GIGABYTE)
        printed += printf("%10.2f MiB", cbyte * 1.0 / (MEGABYTE));
    else
        printed += printf("%10.2f GiB", cbyte * 1.0 / (GIGABYTE));

    // print time
    int h1 = (time / (60 * 60)) / 10;
    int h2 = ((int)time / (60 * 60)) % 10;
    int m1 = ((time - (h1 * 10 + h2) * 60 * 60) / 60) / 10;
    int m2 = (((int)time - (h1 * 10 + h2) * 60 * 60) / 60) % 10;
    int s1 = (time - (h1 * 10 + h2) * 60 * 60 - (m1 * 10 + m2) * 60) / 10;
    int s2 = (time - (h1 * 10 + h2) * 60 * 60 - (m1 * 10 + m2) * 60) % 10;
    printed += printf("%5d%d:%d%d", m1, m2, s1, s2);

    // print bar
    printed += printf("%3c", '[');
    int percent = cbyte * 100 / totalbyte;
    int brick = percent * barlen / 100;
    for (int i = 0; i < brick; i++)
        printed += printf("#");
    for (int i = 0; i < barlen - brick; i++)
        printed += printf("-");
    printed += printf("%c%3d%%", ']', percent);

    fflush(stdout);
    return printed;
}

void clean_progressbar(int pchar)
{
    if (pchar <= 0)
        return;

    // get width height of current terminal
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    printf("%c[J", 27); // clear screen from cursor down

    int line = floor(pchar * 1.0 / w.ws_col);
    for (int i = 0; i < line; i++)
    {
        printf("%c[2K", 27);     // clear line
        printf("%c[%dA", 27, 1); // move cursur up 1 line
    }

    // clear and back to begin line
    printf("%c[2K\r", 27);
}