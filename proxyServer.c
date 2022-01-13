//
// Created by Ido Cohen on 20/12/2021.
//
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include "threadpool.h"

#define USAGE_MSG "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n"
#define CHUNK 1024
#define TRUE 1
#define FALSE 0
#define REQ_TEMPLATE "GET %s %s\r\nHost: %s\r\nConnection: close\r\n\r\n"

struct Headers{
    int *client_fd;
    char *request;
    char *method;
    char *path;
    char *protocol;
    char *host;
};

typedef struct List{
    char *data;
    struct List *next;
}list;

typedef struct Filters{
    list *urlHead;
    list *urlTail;
    list *ipHead;
    list *ipTail;
}filters;

char *get_mime_type(char *name);
int handleRequests(void *sd);
int listenLoop(threadpool *tp, int maxRequests, int port);
void responseErr(int code, int fd);
void freeHeaders(struct Headers *h);
int checkIfExist(char *filePath);
int loadFilterFile(char* filePath);
int searchInFilter(struct in_addr hostIP, char* hostDomain);
void freeFilters();
void createFile(char *fullPath, FILE **newFile);
long readResponseMsg(int server_fd, int client_fd, char *fullPath);
long findFileSize(char *fullPath);
long giveFromLocal(char *fullPath, int client_fd);
void writeFileContent(char *fullPath, int client_fd);

const char BAD_REQUEST[] = "HTTP/1.0 400 Bad Request\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: 113\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\r\n"
                           "<BODY><H4>400 Bad request</H4>\r\n"
                           "Bad Request.\r\n"
                           "</BODY></HTML>";

const char ACCESS_DENIED[] = "HTTP/1.0 403 Forbidden\r\n"
                             "Content-Type: text/html\r\n"
                             "Content-Length: 111\r\n"
                             "Connection: close\r\n"
                             "\r\n"
                             "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\r\n"
                             "<BODY><H4>403 Forbidden</H4>\r\n"
                             "Access denied.\r\n"
                             "</BODY></HTML>";

const char NOT_FOUND[] = "HTTP/1.0 404 Not Found\r\n"
                         "Content-Type: text/html\r\n"
                         "Content-Length: 112\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\r\n"
                         "<BODY><H4>404 Not Found</H4>\r\n"
                         "File not found.\r\n"
                         "</BODY></HTML>";

const char SERVER_ERROR[] = "HTTP/1.0 500 Internal Server Error\r\n"
                            "Content-Type: text/html\r\n"
                            "Content-Length: 144\r\n"
                            "Connection: close\r\n"
                            "\r\n"
                            "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\r\n"
                            "<BODY><H4>500 Internal Server Error</H4>\r\n"
                            "Some server side error.\r\n"
                            "</BODY></HTML>";

const char NOT_SUPPORTED[] = "HTTP/1.0 501 Not supported\r\n"
                             "Content-Type: text/html\r\n"
                             "Content-Length: 129\r\n"
                             "Connection: close\r\n"
                             "\r\n"
                             "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\r\n"
                             "<BODY><H4>501 Not supported</H4>\r\n"
                             "Method is not supported.\r\n"
                             "</BODY></HTML>";

filters *f;

int main(int argc, char *argv[]) {
    if(argc != 5){
        printf(USAGE_MSG);
        return -1;
    }
    char *checkIfNumber;
    int serverPort = (int) strtol(argv[1], &checkIfNumber, 10);
    if(strlen(checkIfNumber) != 0 || serverPort <= 0){
        printf(USAGE_MSG);
        return -1;
    }
    int poolSize = (int) strtol(argv[2], &checkIfNumber, 10);
    if(strlen(checkIfNumber) != 0 || poolSize <= 0 || poolSize > MAXT_IN_POOL){
        printf(USAGE_MSG);
        return -1;
    }
    int maxRequests = (int) strtol(argv[3], &checkIfNumber, 10);
    if(strlen(checkIfNumber) != 0 || maxRequests <= 0){
        printf(USAGE_MSG);
        return -1;
    }
    f = (filters*) malloc(sizeof(filters));
    if(f == NULL){
        perror("error: <sys_call>\n");
        return -1;
    }
    f->ipHead = NULL;
    f->ipTail = NULL;
    f->urlHead = NULL;
    f->urlTail = NULL;
    if(loadFilterFile(argv[4]) == -1){
        freeFilters();
        return -1;
    }
    threadpool *tp = create_threadpool(poolSize);
    if(tp == NULL){
        freeFilters();
        return -1;
    }
    listenLoop(tp, maxRequests, serverPort);

    destroy_threadpool(tp);
    freeFilters();
    return 0;
}
/**
 * Loop that waiting to connections, runs on the main thread.
 * @param tp threadpool pointer
 * @param maxRequests
 * @param port
 * @return 0 - on success
 *         -1 - on error before the listen loop started
 */
int listenLoop(threadpool *tp, int maxRequests, int port){
    struct sockaddr_in srv;

    int fd;

    if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error: <sys_call>\n");
        return -1;
    }

    srv.sin_family = AF_INET;

    srv.sin_port = htons(port);

    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(fd, (struct sockaddr*) &srv, sizeof(srv)) < 0){
        perror("error: <sys_call>\n");
        return -1;
    }
    if(listen(fd, 5) < 0){
        perror("error: <sys_call>\n");
        return -1;
    }
    for (int i = 0; i < maxRequests; i++) {
        int *newFd = (int*) malloc(sizeof(int));
        if(newFd == NULL){
            continue;
        }
        if((*newFd = accept(fd, NULL, NULL)) < 0){
            continue;
        }
        dispatch(tp, &handleRequests, (void*)newFd);
    }
    close(fd);
    return 0;
}

/**
 *
 * @param name - file name
 * @return content type string
 */
char *get_mime_type(char *name){
    char *ext = strrchr(name, '.');

    if (!ext) return NULL;

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";

    return NULL;
}

/**
 *
 * @param sd - the socket descriptor of the client
 * @return -1 - on error
 *          0 - on success
 */
int handleRequests(void *sd){
    struct Headers *h = (struct Headers*) malloc(sizeof(struct Headers));
    if(h == NULL){
        responseErr(4, *(int*)sd);
        return -1;
    }
    h->client_fd = (int*)sd;
    h->request = NULL;

    ssize_t nbytes;
    ssize_t totalBytes = 0;
    char buffer[CHUNK];
    h->request = (char*) malloc(sizeof(char) * (CHUNK + 1));
    if(h->request == NULL){
        responseErr(4, *h->client_fd);
        freeHeaders(h);
        return -1;
    }
    while ((nbytes = read(*h->client_fd, buffer, CHUNK)) > 0){ //read all headers
        buffer[nbytes] = '\0';
        memcpy(h->request + sizeof(char) * totalBytes, buffer, nbytes + 1);
        totalBytes += nbytes;
        if(strstr(buffer, "\r\n\r\n") != NULL){
            break;
        }
        h->request = realloc(h->request, sizeof(char) * (totalBytes + CHUNK + 1));
        if(h->request == NULL){
            responseErr(4, *h->client_fd);
            freeHeaders(h);
            return -1;
        }

    }


    h->method = (char*) malloc(sizeof(char) * (totalBytes + 1));
    h->path = (char*) malloc(sizeof(char) * (totalBytes + 1));
    h->protocol = (char*) malloc(sizeof(char) * (totalBytes + 1));
    h->host = (char*) malloc(sizeof(char) * (totalBytes + 1));

    if(h->method == NULL || h->path == NULL || h->protocol == NULL || h->host == NULL){
        responseErr(4, *h->client_fd);
        freeHeaders(h);
        return -1;
    }

    if(sscanf(h->request, "%s %s %s", h->method, h->path, h->protocol) != 3){
        responseErr(1, *h->client_fd);
        freeHeaders(h);
        return -1;
    }

    if(strcasecmp(h->protocol, "HTTP/1.0") != 0 && strcasecmp(h->protocol, "HTTP/1.1") != 0){
        responseErr(1, *h->client_fd);
        freeHeaders(h);
        return -1;
    }

    char *firstPtr = strstr(h->request, "Host: ");
    if(firstPtr == NULL){
        responseErr(1, *h->client_fd);
        freeHeaders(h);
        return -1;
    }
    firstPtr += strlen("Host: ");
    char *secondPtr = strchr(firstPtr, '\r');
    if(secondPtr == NULL || (secondPtr - firstPtr) == 0){
        responseErr(1, *h->client_fd);
        freeHeaders(h);
        return -1;
    }

    snprintf(h->host, secondPtr - firstPtr + 1, "%s", firstPtr);
    if(strcasecmp(h->method, "GET") != 0){
        responseErr(5, *h->client_fd);
        freeHeaders(h);
        return -1;
    }
    struct hostent *hp;
    struct in_addr address;
    hp = gethostbyname(h->host);
    if (hp == NULL){    //check if the URL/IP is valid
        responseErr(3, *h->client_fd);
        freeHeaders(h);
        return -1;
    }
    address.s_addr = ((struct in_addr*)(hp->h_addr))->s_addr;
    if(searchInFilter(*((struct in_addr *) (hp->h_addr)), h->host) == TRUE){
        responseErr(2, *h->client_fd);
        freeHeaders(h);
        return -1;
    }

    char *pathToSearch = h->path;
    if(strstr(pathToSearch, "http://") == pathToSearch){    //remove the "http://" and the host name to search/create local file.
        pathToSearch = pathToSearch + strlen("http://");
    }
    if(strstr(pathToSearch, h->host) == pathToSearch){
        pathToSearch = pathToSearch + strlen(h->host);
    }
    char *fullPath = (char*)malloc(sizeof(char) * (strlen(h->host) + strlen(pathToSearch) + strlen("index.html") + 1));
    if (fullPath == NULL){
        responseErr(4, *h->client_fd);
        freeHeaders(h);
        return -1;
    }
    if(pathToSearch[strlen(pathToSearch) - 1] == '/'){
        sprintf(fullPath, "%s%s%s", h->host, pathToSearch, "index.html");
    }else {
        sprintf(fullPath, "%s%s", h->host, pathToSearch);
    }
    char *constructedRequest = (char*)malloc(sizeof(char)*(strlen(h->path) + strlen(h->protocol) + strlen(h->host) + strlen(REQ_TEMPLATE) + 1));
    if(constructedRequest == NULL){
        free(fullPath);
        responseErr(4, *h->client_fd);
        freeHeaders(h);
        return -1;
    }
    sprintf(constructedRequest, REQ_TEMPLATE, h->path, h->protocol, h->host);
    printf("HTTP request =\n%s\nLEN = %d\n", constructedRequest, (int)strlen(constructedRequest));
    if(checkIfExist(fullPath) == TRUE){ //from local
        printf("File is given from local filesystem\n");
        printf("\n Total response bytes: %d\n", (int)giveFromLocal(fullPath, *h->client_fd));

    }
    else{   //from server
        struct sockaddr_in peeraddr;
        int server_fd;

        if ((server_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
            free(fullPath);
            free(constructedRequest);
            responseErr(4, *h->client_fd);
            freeHeaders(h);
            return -1;
        }
        peeraddr.sin_family = AF_INET;
        peeraddr.sin_port = htons(80);
        peeraddr.sin_addr.s_addr = address.s_addr;

        if(connect(server_fd, (struct sockaddr*) &peeraddr, sizeof(peeraddr)) < 0) {
            free(fullPath);
            free(constructedRequest);
            responseErr(4, *h->client_fd);
            freeHeaders(h);
            return -1;
        }
        size_t written = 0;

        while (written < strlen(constructedRequest)) {  //send the request to the server
            if ((nbytes = write(server_fd, constructedRequest + written, strlen(constructedRequest) - written)) < 0) {
                free(fullPath);
                free(constructedRequest);
                responseErr(4, *h->client_fd);
                freeHeaders(h);
                return -1;
            }
            written += nbytes;
        }
        long responseBytes = readResponseMsg(server_fd, *h->client_fd, fullPath);
        if (responseBytes == -1){
            free(fullPath);
            free(constructedRequest);
            responseErr(4, *h->client_fd);
            freeHeaders(h);
            return -1;
        }
        printf("File is given from origin server\n");
        printf("\n Total response bytes: %d\n", (int)responseBytes);
    }
    free(constructedRequest);

    free(fullPath);
    freeHeaders(h);

    return 0;
}

void responseErr(int code, int fd){
    switch (code) {
        case 1:
            write(fd, BAD_REQUEST, strlen(BAD_REQUEST));
            break;
        case 2:
            write(fd, ACCESS_DENIED, strlen(ACCESS_DENIED));
            break;
        case 3:
            write(fd, NOT_FOUND, strlen(NOT_FOUND));
            break;
        case 4:
            write(fd, SERVER_ERROR, strlen(SERVER_ERROR));
            break;
        case 5:
            write(fd, NOT_SUPPORTED, strlen(NOT_SUPPORTED));
            break;
        default:
            break;
    }
    close(fd);
}

void freeHeaders(struct Headers *h){
    if(h->client_fd != NULL){
        close(*h->client_fd);
        free(h->client_fd);
        h->client_fd = NULL;
    }
    if(h->request != NULL){
        free(h->request);
        h->request = NULL;
    }
    if(h->protocol != NULL){
        free(h->protocol);
        h->protocol = NULL;
    }
    if(h->method != NULL){
        free(h->method);
        h->method = NULL;
    }
    if(h->path != NULL){
        free(h->path);
        h->path = NULL;
    }
    if(h->host != NULL){
        free(h->host);
        h->host = NULL;
    }
    free(h);

}

int loadFilterFile(char* filePath){
    if (checkIfExist(filePath) == FALSE){
        printf(USAGE_MSG);
        return -1;
    }
    f->ipHead = NULL;
    f->ipTail = NULL;
    f->urlHead = NULL;
    f->urlTail = NULL;

    FILE *fp = fopen(filePath, "r");
    if(fp == NULL){
        perror("error: <sys_call>\n");
        return -1;
    }
    char *line = NULL;
    size_t len = 0;
    while (getline(&line, &len, fp) != -1){
        list *temp = (list*) malloc(sizeof(list));
        if (temp == NULL){
            fclose(fp);
            perror("error: <sys_call>\n");
            return -1;
        }
        temp->next = NULL;
        temp->data = (char*) malloc(sizeof(char) * (strlen(line) + 1));
        if (temp->data == NULL){
            fclose(fp);
            perror("error: <sys_call>\n");
            return -1;
        }
        if(strchr(line, '\r') != NULL){
            *strchr(line, '\r') = '\0';
        }
        if(strchr(line, '\n') != NULL){
            *strchr(line, '\n') = '\0';
        }
        sprintf(temp->data, "%s", line);
        if(*line >= '0' && *line <= '9'){
            if (f->ipHead == NULL){
                f->ipHead = temp;
                f->ipTail = temp;
            } else{
                f->ipTail->next = temp;
                f->ipTail = temp;
            }
        } else{
            if (f->urlHead == NULL){
                f->urlHead = temp;
                f->urlTail = temp;
            } else{
                f->urlTail->next = temp;
                f->urlTail = temp;
            }
        }
    }
    fclose(fp);
    free(line);
    return 1;
}

int searchInFilter(struct in_addr hostIP, char* hostDomain){
    list *curr = f->urlHead;
    while(curr != NULL){
        if(strcasecmp(hostDomain, curr->data) == 0){
            return TRUE;
        }
        curr = curr->next;
    }
    curr = f->ipHead;
    while(curr != NULL){
        int c1, c2, c3, c4, bitsNum, scanned;
        struct in_addr ip;
        scanned = sscanf(curr->data, "%d.%d.%d.%d/%d", &c1, &c2, &c3, &c4, &bitsNum);
        ip.s_addr = htonl(c4 + 256*c3 + 256*256*c2 + 256*256*256*c1);
        if(scanned == 4){
            bitsNum = 32;
        }
        if(((ip.s_addr ^ hostIP.s_addr) & htonl(0xFFFFFFFFu << (32 - bitsNum))) == 0){ //compare bitsNum bits
            return TRUE;
        }
        curr = curr->next;
    }
    return FALSE;
}

int checkIfExist(char *filePath) {
    struct stat sb;
    if (stat(filePath, &sb) == 0 && S_ISREG(sb.st_mode)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void freeFilters(){
    list *curr = f->urlHead;
    while (curr != NULL){
        if (curr->data != NULL){
            free(curr->data);
        }
        list *temp = curr->next;
        free(curr);
        curr = temp;
    }
    curr = f->ipHead;
    while (curr != NULL){
        if (curr->data != NULL){
            free(curr->data);
        }
        list *temp = curr->next;
        free(curr);
        curr = temp;
    }
    free(f);
    f = NULL;
}

/**
 * read the response from the server and write to the client and file
 * @param server_fd
 * @param client_fd
 * @param fullPath
 * @return How many bytes written
 */
long readResponseMsg(int server_fd, int client_fd, char *fullPath) {
    FILE *newFile;
    createFile(fullPath, &newFile);
    if(newFile == NULL){
        return -1;
    }
    char *msg = malloc(sizeof(char) * (CHUNK + 1));
    if (msg == NULL){
        return -1;
    }
    unsigned char buf [CHUNK];
    memset(msg, '\0', CHUNK + 1);
    ssize_t nbytes;
    size_t totalBytes = 0;
    char *endOfHeaders;
    int isFdLive = TRUE;

    while ((endOfHeaders = strstr((char*)msg, "\r\n\r\n")) == NULL){ //read response headers and write them to the client
        nbytes = read(server_fd, buf, CHUNK);
        if(nbytes < 0) {
            free(msg);
            fclose(newFile);
            remove(fullPath);
            return (long)totalBytes;
        }
        if(isFdLive == TRUE){
            if(write(client_fd, buf, nbytes) < 0){
                isFdLive = FALSE;
            }
        }

        memcpy(msg + totalBytes, buf, nbytes);
        totalBytes += nbytes;
        msg[totalBytes] = '\0';
        if((msg = realloc(msg, sizeof(char) * (totalBytes + CHUNK + 1))) == NULL){
            fclose(newFile);
            remove(fullPath);
            return (long)totalBytes;
        }
    }
    *endOfHeaders = '\0';

    int startOfContent = (int)(strlen(msg) + 4 + nbytes - totalBytes);
    fwrite(buf + startOfContent, 1, nbytes - startOfContent, newFile); //write to file the content after the headers from the last chunk
    while ((nbytes = read(server_fd, buf, CHUNK)) > 0){ //write the rest of the content to the client and the file
        if(fwrite(buf, 1, nbytes, newFile) != (size_t)nbytes){
            fclose(newFile);
            remove(fullPath);
            return (long)totalBytes;
        }
        totalBytes += nbytes;
        if (isFdLive == TRUE){
            if(write(client_fd, buf, nbytes) < 0){
                isFdLive = FALSE;
            }
        }
    }
    fclose(newFile);
    free(msg);
    return (long)totalBytes;
}

/**
 * create file and sub folders
 * @param fullPath
 * @param newFile
 */
void createFile(char *fullPath, FILE **newFile) {
    char *p = NULL;
    for (p = fullPath; *p && strstr(p, "/") != NULL; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(fullPath, S_IRWXU);
            *p = '/';
        }
    }
    *newFile = fopen(fullPath, "wb");
    if (*newFile == NULL) {
        return;
    }
}

long findFileSize(char *fullPath) {
    FILE* fp = fopen(fullPath, "r");
    fseek(fp, 0L, SEEK_END);
    long int res = ftell(fp);
    fclose(fp);
    return (long)res;
}

/**
 * write file content to the client from local file system
 * @param fullPath
 * @param client_fd
 */
void writeFileContent(char *fullPath, int client_fd) {
    char buf[CHUNK];
    FILE *fp;
    size_t nread;
    fp = fopen(fullPath, "r");
    if (fp == NULL) {
        return;
    }
    while ((nread = fread(buf, 1, sizeof buf, fp)) > 0) {
        if(write(client_fd, buf, nread) < 0){
            fclose(fp);
            return;
        }
    }
    fclose(fp);
}

/**
 * write headers and file content(from local file system) to the client
 * @param fullPath
 * @param client_fd
 * @return how meany bytes writen to the client
 */
long giveFromLocal(char *fullPath, int client_fd) {
    char msg[CHUNK];
    char *type = get_mime_type(fullPath);
    long len = findFileSize(fullPath);
    if (type != NULL){
        sprintf(msg, "HTTP/1.0 200 OK\r\nContent-Length: %ld\r\nContent-Type: %s\r\nConnection: close\r\n\r\n", len, type);
    } else{
        sprintf(msg, "HTTP/1.0 200 OK\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", len);
    }

    if(write(client_fd, msg, strlen(msg)) < 0){
        return 0;
    }
    writeFileContent(fullPath, client_fd);

    return len + strlen(msg);
}
