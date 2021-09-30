#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>

#define BUF_LEN_10B   10
#define BUF_LEN_512B   512
#define BUF_LEN_1KB   1024
#define STDIN   0
#define STDOUT  1
#define STDERR  2
typedef struct {
    char method[BUF_LEN_10B];
    char url[BUF_LEN_512B];
    size_t content_length;
    char data[BUF_LEN_1KB];
} recvdata_t;

static int startup(int port);
void accept_request(void *arg);
static size_t get_line(int client, char *buf, int len);
static int get_data(int client, recvdata_t *recvdata);
static int get_command(int client, recvdata_t *recvdata);
static int set_html_file(int client, recvdata_t *recvdata);
static int set_cgi_file(int client, recvdata_t *recvdata);

void main()
{
    int server_sock = -1;
    int client_sock = -1;
    int port = 4000;
    struct sockaddr_in client_addr;
    size_t client_addr_len = sizeof(client_addr);
    pthread_t thread;
    
    server_sock = startup(port);
    printf("server port:%d\n", port);
    while (1) {
        if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len)) < 0) {
            perror("accept error");
            exit(1);
        }
        printf("client address:%s port:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        if (pthread_create(&thread, NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0) {
            fprintf(stderr, "pthread_create error");
            exit(1);
        }
    }
    close(server_sock);
}

void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    recvdata_t recvdata = {'\0', '\0', 0, '\0'};
    if (get_command(client, &recvdata) < 0) {
        fprintf(stderr, "accept_request:get_command error");
        close(client);
        return;
    }
    if (strncasecmp("GET", recvdata.method, 3) && strncasecmp("POST", recvdata.method, 4)) {
        close(client);
        return;
    }
    if (get_data(client, &recvdata) < 0) {
        fprintf(stderr, "accept_request:get_data error");
        close(client);
        return;
    }
    if (!strncasecmp("GET", recvdata.method, 3)) {
        set_html_file(client, &recvdata);
    } else {
        set_cgi_file(client, &recvdata);
    }
    close(client);
}

static size_t get_line(int client, char *buf, int len)
{
    char chr = '\0';
    int n = 1;
    int i = 0;
    while (chr != '\n' && i < len-1) {
        n = recv(client, &chr, 1, 0);
        if (n > 0) {
            if (chr == '\r') {
                n = recv(client, &chr, 1, 0);
                if (chr != '\n') {
                    chr = '\n';
                }
            }
            buf[i] = chr;
            i++;
        } else {
            chr = '\n';
            return -1;
        }
    }
    buf[i] = '\0';
    return i;
}

static int get_data(int client, recvdata_t *recvdata)
{
    char buf[BUF_LEN_512B] = {'\0'};
    char *info = "Content-length:";
    int info_len = strlen(info);
    int numchars = 1;
    int i;
    if (!strncasecmp("GET", recvdata->method, 3)) {
        while (strncasecmp("\n", buf, 1) && numchars > 0) {
            numchars = get_line(client, buf, BUF_LEN_512B);
        }
        return 0;
    }
    while (strncasecmp(info, buf, info_len) && numchars > 0) {
        numchars = get_line(client, buf, BUF_LEN_512B);
    }
    if (numchars <= 0) {
        return numchars;
    }
    recvdata->content_length = atoi(&buf[info_len]);
    while (strncasecmp("\n", buf, 1) && numchars > 0) {
        numchars = get_line(client, buf, BUF_LEN_512B);
    }
    if (numchars <= 0) {
        return numchars;
    }
    numchars = recv(client, recvdata->data, recvdata->content_length, 0);
    recvdata->data[recvdata->content_length] = '\0';
    return 0;
}

static int get_command(int client, recvdata_t *recvdata)
{
    char buf[BUF_LEN_1KB] = {'\0'};
    int numchars = get_line(client, buf, BUF_LEN_1KB);
    int i, j;
    if (numchars < 0) {
        return numchars;
    }
    for (i = 0; i < BUF_LEN_10B && i < numchars; i++) {
        if (buf[i] == ' ')break;
        recvdata->method[i] = buf[i];
    }
    recvdata->method[i] = '\0';
    for (j = 0, i += 1; j < BUF_LEN_1KB && i < numchars; i++, j++) {
        if (buf[i] == ' ')break;
        recvdata->url[j] = buf[i];
    }
    recvdata->url[j] = '\0';
    if (recvdata->url[j-1] == '/') {
        strncat(recvdata->url, "index.html", 10);
    }
    return 0;
}

static int set_html_file(int client, recvdata_t *recvdata)
{
    int fp = -1;
    char path[BUF_LEN_512B] = {'\0'};
    char buf[BUF_LEN_1KB] = {'\0'};
    char *info = "HTTP/1.1 200 OK\r\nHost:shm\r\nContent-type:text/html\r\n\r\n"; 
    int numchars = 0;
    sprintf(path, "htdocs%s", recvdata->url);
    if ((fp = open(path, O_RDONLY)) < 0) {
        perror("open error");
        return -1;
    }
    send(client, info, strlen(info), 0);
    while ((numchars = read(fp, buf, BUF_LEN_1KB)) > 0) {
        send(client, buf, numchars, 0);
    }
    close(fp);
    return 0;
}

static int set_cgi_file(int client, recvdata_t *recvdata)
{
    int fp = -1;
    char path[BUF_LEN_512B] = {'\0'};
    char buf[BUF_LEN_1KB] = {'\0'};
    char *info = "HTTP/1.1 200 OK\r\n"; 
    int numchars = 0;
    int output[2];
    int pid = -1;
    sprintf(path, "htdocs%s", recvdata->url);
    if ((fp = open(path, O_RDONLY)) < 0) {
        perror("open error");
        return -1;
    }
    close(fp);
    if (pipe(output) < 0) {
        perror("pipe error");
        return -1;
    }
    pid = fork();
    if (pid == 0) {
        close(output[0]);
        dup2(output[1], STDOUT);
        sprintf(buf, "REQUEST_METHOD=%s", recvdata->method);
        putenv(buf);
        sprintf(buf, "CONTENT_LENGTH=%d", recvdata->content_length);
        putenv(buf);
        execl(path, recvdata->url, recvdata->data, NULL);
        exit(0);
    } else {
        close(output[1]);
        send(client, info, strlen(info), 0);
        while ((numchars = read(output[0], buf, BUF_LEN_1KB)) > 0) {
            send(client, buf, numchars, 0);
        }
        close(output[0]);
        waitpid(&pid, NULL, 0);
    }
}

static int startup(int port)
{
    int sockfd = -1;
    struct sockaddr_in seraddr;
    int opt = 1;
    seraddr.sin_family = AF_INET;
    seraddr.sin_port = htons(port);
    seraddr.sin_addr.s_addr = INADDR_ANY;
    
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        exit(1);
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt error");
        exit(1);
    }
    if (bind(sockfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) < 0) {
        perror("bind error");
        exit(1);
    }
    if (listen(sockfd, SOMAXCONN) < 0) {
        perror("listen error");
        exit(1);
    }
    return sockfd;
}

