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

#define BUF_MAX_LEN         1024
#define METHOD_MAX_LEN      10
#define URL_MAX_LEN         512

#define SERVER_OK            0
#define ERR_ARGS            -1
#define ERR_CLIENT_CLOSE    -2
#define ERR_API             -3

#define STDIN   0
#define STDOUT  1
#define STDERR  2
typedef struct {
    char *method;
    char *url;
    size_t content_length;
    char *data;
} recvdata_t;


void *accept_request(void *arg);
static int send_header(int fd);
static int send_html_file(int fd, char *path);
static int send_cgi_file(int fd, char *path, recvdata_t *rdata);
static int get_request(int fd, recvdata_t *rdata);
static int get_data_length(int fd, recvdata_t *rdata);
static int get_recv_data(int fd, recvdata_t *rdata);
static ssize_t get_recv_line(int fd, char *buf, size_t len);
static int discard_recv_data(int fd);
static int startup(int port);

void main()
{
    int sockfd = -1, fd = -1;
    struct sockaddr_in cliaddr;
    int addrlen = sizeof(cliaddr);
    pthread_t thread;
    pid_t pid;
    sockfd = startup(4000);
    for (;;) {
        if ((fd = accept(sockfd, (struct sockaddr *)&cliaddr, (socklen_t *)&addrlen)) < 0) {
            perror("accept error");
            exit(1);
        }
        printf("client address:%s port:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
        
        if (pthread_create(&thread, NULL, accept_request, (void *)(intptr_t)fd) != 0) {
            fprintf(stderr, "pthread_create error");
            exit(1);
        }
    }
    close(sockfd);
}

void *accept_request(void *arg)
{
    int fd = (intptr_t)arg;
    char path[URL_MAX_LEN] = {'\0'};
    recvdata_t recvdata = {NULL, NULL, 0, NULL};

    if (get_request(fd, &recvdata) < 0) {
        fprintf(stderr, "accept_request:get_request error");
        return NULL;
    }
    if (strncmp("GET", recvdata.method, 3) && strncmp("POST", recvdata.method, 4)) {
        fprintf(stderr, "accept_request:recvdata.method error");
        return NULL;
    }
    sprintf(path, "htdocs%s", recvdata.url);
    if (!strncmp("POST", recvdata.method, 4)) {
        if (get_data_length(fd, &recvdata) < 0) {
            fprintf(stderr, "accept_request:get_data_length error");
            return NULL;
        }
        if (get_recv_data(fd, &recvdata) < 0) {
            fprintf(stderr, "accept_request:get_recv_data error");
            return NULL;
        }
        
        char *info = "HTTP/1.0 200 OK\r\n"; 
        send(fd, info, strlen(info), 0);
        if (send_cgi_file(fd, path, &recvdata) < 0) {
            fprintf(stderr, "accept_request:send_html_file error");
            return NULL;
        }
    } else {
        if (discard_recv_data(fd) < 0) {
            fprintf(stderr, "accept_request:discard_recv_data error");
            return NULL;
        }
        send_header(fd);
        if (send_html_file(fd, path) < 0) {
            fprintf(stderr, "accept_request:send_html_file error");
            return NULL;
        }
    }
    close(fd);
    if (recvdata.method) {
        free(recvdata.method);
    }
    if (recvdata.url) {
        free(recvdata.url);
    }
    if (recvdata.data) {
        free(recvdata.data);
    }
    return NULL;
}

static int send_header(int fd)
{
    char *info = "HTTP/1.1 200 OK\r\nHost:shm\r\nContent-type:text/html\r\n\r\n";
    size_t info_len = strlen(info);
    send(fd, info, info_len, 0);
    return 0;
}

static int send_html_file(int fd, char *path)
{
    char buf[BUF_MAX_LEN] = {'\0'};
    int fp = -1;
    int ret = -1;
    if ((fp = open(path, O_RDONLY)) < 0) {
        perror("send_html_file: open error");
        return ERR_API;
    }
    if ((ret = read(fp, buf, BUF_MAX_LEN)) > 0) {
        send(fd, buf, ret, 0);
    }
    close(fp);
    return 0;
}

static int send_cgi_file(int fd, char *path, recvdata_t *rdata)
{
    pid_t pid;
    int input[2];
    char buf[BUF_MAX_LEN] = {'\0'};
    int ret = -1;
    if (pipe(input) < 0 ) {
        perror("send_cgi_file: pipe error");
        return ERR_API;
    }
    pid = fork();
    if (pid == 0) { // child
        close(input[0]);
        dup2(input[1], STDOUT);
        sprintf(buf, "REQUEST_METHOD=%s", rdata->method); 
        putenv(buf);
        sprintf(buf, "CONTENT_LENGTH=%d", rdata->content_length); 
        putenv(buf);
        execl(path, rdata->url, rdata->data, NULL);
        exit(0);
    } else {
        close(input[1]);
        while ((ret = read(input[0], buf, BUF_MAX_LEN)) > 0) {
            send(fd, buf, ret, 0);
        }
        waitpid(pid, NULL, 0);
    }
    return 0;
}

static int get_request(int fd, recvdata_t *rdata)
{
    char buf[BUF_MAX_LEN] = {'\0'};
    char *method = NULL;
    char *url = NULL;
    char *ptr = NULL;
    ssize_t numchars = -1;
    int i = 0;
    numchars = get_recv_line(fd, buf, BUF_MAX_LEN);
    if (numchars < 0) {
        return numchars;
    }
    
    method = (char *)malloc(METHOD_MAX_LEN);
    if (!method) {
        perror("get_request: malloc error");
        return ERR_API;
    }
    rdata->method = method;
    
    url = (char *)malloc(URL_MAX_LEN);
    if (!url) {
        perror("get_request: malloc error");
        free(method);
        return ERR_API;
    }
    rdata->url = url;
    
    ptr = buf;
    for (i = 0; i < (METHOD_MAX_LEN-1) && ptr < (buf+numchars); i++) {
        if (ptr[i] == ' ') {
            break;
        }
        method[i] = ptr[i];
    }
    method[i] = '\0';
    
    ptr = &buf[i+1];
    for (i = 0; i < (URL_MAX_LEN-1) && ptr < (buf+numchars); i++) {
        if (ptr[i] == ' ') {
            break;
        }
        url[i] = ptr[i];
    }
    url[i] = '\0';
    if (url[i-1] == '/') {
        strncat(url, "index.html", 10);
    }
    return 0;
}

static int get_data_length(int fd, recvdata_t *rdata)
{
    char buf[BUF_MAX_LEN] = {'\0'};
    char *length_chr = "Content-Length:";
    size_t length_len = strlen(length_chr);
    size_t content_length = 0;
    ssize_t numchars = -1;
    while ((numchars = get_recv_line(fd, buf, BUF_MAX_LEN)) > 0 && strncmp(buf, "\n", 1)) {
        if (strncmp(length_chr, buf, length_len)) {
            continue;
        }
        content_length = atoi(&buf[length_len]);
    }
    if (numchars < 0) {
        return numchars;
    }
    rdata->content_length = content_length;
    return 0;
}

static int get_recv_data(int fd, recvdata_t *rdata)
{
    char *data = NULL;
    char chr;
    size_t chrlen = sizeof(chr);
    ssize_t ret = -1;
    ssize_t k = 0;
    
    data = (char *)malloc(rdata->content_length+1);
    if (!data) {
        perror("get_data: malloc error");
        return ERR_API;
    }
    while (k < (rdata->content_length) && (ret = recv(fd, &chr, chrlen, 0)) > 0) {
        data[k++] = chr;
    }
    data[k] = '\0';
    if (ret < 0) {
        free(data);
        perror("get_recv_data: recv error");
        return ERR_API;
    } else if (ret == 0) {
        free(data);
        fprintf(stderr, "get_recv_data:client close");
        return ERR_CLIENT_CLOSE;
    }
    rdata->data = data;
    return 0;
}

// 数组太小导致未接受到一行呢？
static ssize_t get_recv_line(int fd, char *buf, size_t len)
{
    char chr;
    size_t chrlen = sizeof(chr);
    ssize_t ret = -1;
    ssize_t k = 0;
    
    while ((ret = recv(fd, &chr, chrlen, 0)) > 0 && k < (len-1)) {
        if (chr == '\r') {
            ret = recv(fd, &chr, chrlen, 0);
            buf[k++] = chr;
            break;
        }
        buf[k++] = chr;
    }
    buf[k] = '\0';
    if (ret < 0) {
        perror("get_recv_line: recv error");
        return ERR_API;
    } else if (ret == 0) {
        fprintf(stderr, "get_recv_line:client close");
        return ERR_CLIENT_CLOSE;
    }
    return (k+1);
}

static int discard_recv_data(int fd)
{
    char buf[BUF_MAX_LEN] = {'\0'};
    int ret = -1;
    size_t len = BUF_MAX_LEN;
    
    while ((ret = get_recv_line(fd, buf, len)) > 0 && strncmp(buf, "\n", 1));
    if (ret < 0) {
        perror("discard_recv_data: get_recv_line error");
        return ERR_API;
    } else if (ret == 0) {
        fprintf(stderr, "discard_recv_data:client close");
        return ERR_CLIENT_CLOSE;
    }
    return SERVER_OK;
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
