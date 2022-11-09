#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 9999
#define CACHE_OBJS_COUNT 10
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";
void *thread(void *vargp);

void doit(int connfd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);

/*cache function*/
void cache_init();
int cache_find(char *url);
int cache_eviction();
void cache_LRU(int index);
void cache_uri(char *uri,char *buf);
void readerPre(int i);
void readerAfter(int i);

typedef struct {
    char cache_obj[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];
    int LRU;        // 캐시가 저장된 순서 판단하는 멤버: 10개 넘었을 때, 축출하는 근거로 활용
    int isEmpty;    // 캐시가 비어있는지 여부

    
    int readCnt;      // 현재 cache를 읽고 있는 reader의 수 
    sem_t wmutex;     // 한번에 한 스레드만 writing할 수 있도록 보호조치(semaphore)
    sem_t rdcntmutex; // readCnt(현재 reader 수)를 조작할 때 한번에 한 스레드만 조작할 수 있도록 보호조치(semaphore)

} cache_block;

typedef struct {
    /* 캐시들을 관리하는 구조체: 캐시 10개 */
    cache_block cacheobjs[CACHE_OBJS_COUNT];  /*ten cache blocks*/
    int cache_num;
} Cache;

Cache cache;


int main(int argc,char **argv)
{
    int listenfd,connfd;
    socklen_t  clientlen;
    char hostname[MAXLINE],port[MAXLINE];
    pthread_t tid; //TID 저장 용도(unsigned long int)
    struct sockaddr_storage clientaddr;/* generic sockaddr struct which is 28 Bytes.The same use as sockaddr */

    // 캐시 초기화
    cache_init();

    if(argc != 2){
        fprintf(stderr,"usage :%s <port> \n",argv[0]);
        exit(1);
    }

    // 무슨 코드..? --> 자식 프로세스 생성 안하니까 필요 없는거 아닌가..??
    Signal(SIGPIPE,SIG_IGN);

    // Open a listening socket
    listenfd = Open_listenfd(argv[1]);

    while(1){
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);

        /* print accepted message */
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        printf("Accepted connection from (%s %s).\n",hostname,port);

        /* concurrent request using thread */
        // detatch mode, doit()
        Pthread_create(&tid,NULL,thread,(void *)connfd); //thread함수 호출하는데, 인자로 connfd 넣음
    }
    return 0;
}

/* thread function */
// 각 스레드마다 실질적인 doit() 함수 실행 --> 트랜잭션 처리
void *thread(void *vargp){
    int connfd = (int)vargp;
    Pthread_detach(pthread_self()); //detached thread로 만들어 주기 : 종료시 커널이 알아서 처리해줌(detached thread는 커널에서 자동으로 처리해줌 953p)
    // 쓰레드는 pthread_detach 인자로 pthread_self()를 사용해서 자기자신을 분리할 수 있음
    doit(connfd); 
    Close(connfd);
}

/* handle the client HTTP transaction */
void doit(int connfd)
{
    int end_serverfd; /* the end server file descriptor */

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header [MAXLINE];

    /* store the request line arguments */
    char hostname[MAXLINE], path[MAXLINE];
    int port;

    rio_t rio,server_rio; /* rio is client's rio,server_rio is endserver's rio */

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version); /* read the client request line */

    char url_store[100];
    strcpy(url_store,uri);  /* store the original url */
    // 왜 uri_store가 아니고 url_store인가..

    if(strcasecmp(method,"GET")){
        printf("Proxy does not implement the method");
        return;
    }

    /* check if the uri is cached */
    // uri가 기존 캐시에 있드면, 
    int cache_index;
    if((cache_index=cache_find(url_store))!=-1){/*in cache then return the cache content*/
        // 이미 캐시에 존재하는 uri이면 저장된 정보 전송
        // 이 과정에서 reader를 writer보다 우선시함(한명이라도 읽는 사람있으면 쓰기 차단)
        // 970p 12.26 reader-writer문제의 해답 코드 형식
        readerPre(cache_index); 
        Rio_writen(connfd,cache.cacheobjs[cache_index].cache_obj,strlen(cache.cacheobjs[cache_index].cache_obj));
        // 캐시 오브젝트에 reader로 들어가서, 읽은 내역을 connfd에 write(client에게 전송)
        readerAfter(cache_index); //reader가 1명이라도 있으면 write는 계속 lock 상태
        return;
    }

    /*
     * 이 밑으로는 캐시에 없는 uri 처리: 기존 part1, 2와 동일
     * 마지막에 처리한 뒤 캐시에 저장하는 부분 추가됨
     */
    /* parse the uri to get hostname, file path, port */
    parse_uri(uri, hostname, path, &port);

    /* build the http header which will send to the end server */
    build_http_header(endserver_http_header, hostname, path, port, &rio);

    /* connect to the end server */
    end_serverfd = connect_endServer(hostname, port, endserver_http_header);
    if(end_serverfd<0){
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio, end_serverfd);

    /* write the http header to endserver */
    Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

    /* receive message from end server and send to the client */
    char cachebuf[MAX_OBJECT_SIZE];
    int sizebuf = 0;
    size_t n;
    while((n=Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
    {
        sizebuf+=n;
        if(sizebuf < MAX_OBJECT_SIZE)  strcat(cachebuf, buf);
        Rio_writen(connfd, buf, n);
    }

    Close(end_serverfd);

    /* 모두 처리한 뒤 캐시에 저장 */
    if(sizebuf < MAX_OBJECT_SIZE){
        cache_uri(url_store,cachebuf);
    }
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {

    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

    /* request line */
    sprintf(request_hdr, requestlint_hdr_format, path);

    /* get other request header for client rio and change it */
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {

        /* End of request header */
        if (strcmp(buf, endof_hdr) == 0)  break;

        /* Host */
        if (!strncasecmp(buf, host_key, strlen(host_key))){
            strcpy(host_hdr, buf);
            continue;
        }
        /* Other headers: Connection, Proxy-Connection, User-Agent */
        if (!strncasecmp(buf, connection_key, strlen(connection_key))
                &&!strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
                &&!strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
            strcat(other_hdr, buf);
        }
    }

    /* fill in Host-tag if it is empty */
    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, host_hdr_format, hostname);
    }

    /* concatenate all header tags into http_header */
    sprintf(http_header,"%s%s%s%s%s%s%s",
            request_hdr,    // "GET file-path HTTP/1.0"
            host_hdr,       // "Host"
            conn_hdr,       // "Connection: close\r\n"
            prox_hdr,       // Proxy-Connection: close\r\n"
            user_agent_hdr, // "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n"
            other_hdr,      // "Connection; Proxy-Connection; User-Agent"
            endof_hdr);     // "\r\n"
    return;
}


/* Connect to the end server */
// inline함수: 코드를 그대로 복제 --> 실행속도가 빠르나, 코드가 길어져 실행파일의 크기가 커진다,
inline int connect_endServer(char *hostname, int port, char *http_header){
    char portStr[100];
    sprintf(portStr, "%d", port);
    return Open_clientfd(hostname, portStr);    // end_server에게 proxy는 client
}

/* parse the uri to get hostname, file path, port */
void parse_uri(char *uri, char *hostname, char *path, int *port) {
    // 일반적인 URI format: http://host:port/path?query_string

    *port = 80;
    char* pos = strstr(uri, "//");

    pos = pos != NULL ? pos+2 : uri;    // pos: uri에서 "http://" 바로 뒷부분

    char *pos2 = strstr(pos, ":");      // port번호 있는지 여부
    // port번호 있을때: "host:port/path?query_string"
    if (pos2 != NULL) {
        *pos2 = '\0';
        sscanf(pos, "%s", hostname);        // "host"
        sscanf(pos2+1, "%d%s", port, path); // "port/path?query_string"
    }
    // port번호 없을때: "host/path?query_string"
    else {
        pos2 = strstr(pos, "/");
        // path 있을때
        if (pos2 != NULL) {
            *pos2 = '\0';       // 먼저 host알아내기 위해 문자열 분리
            sscanf(pos,"%s", hostname);
            *pos2 = '/';        // '/'복구 후 path기록
            sscanf(pos2,"%s", path);
        }
        // path 없을때: "host"
        else {
            sscanf(pos,"%s", hostname);
        }
    }
}


/**************************************
 * Cache Functions
 **************************************/

// 캐시들(10개) 초기화하는 함수
void cache_init(){
    cache.cache_num = 0;
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){
        cache.cacheobjs[i].LRU = 0;
        cache.cacheobjs[i].isEmpty = 1;
        Sem_init(&cache.cacheobjs[i].wmutex, 0, 1);
        Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1);
        cache.cacheobjs[i].readCnt = 0;
    }
}


// reading 과정(교과서 971p. 12.26그림)
// readcnt를 rdcntmutex semaphore로 보호하고
// reader가 없을 때에만 writing을 수행할 수 있도록 보호조치를 해놓았음
/*
 * readerPre()함수
 * actual reading...
 * readerAfter()함수
 * 
 */
void readerPre(int i){
    // readCnt 제어
    P(&cache.cacheobjs[i].rdcntmutex); //캐시 obj의 i번째 캐시  
    // reader의 수 하나 증가
    cache.cacheobjs[i].readCnt++; // 971쪽 readcnt++ 의미
    // 첫 reader가 등장하면(readCnt: 0 -> 1) writing lock을 걸어준다: P(&w)
    if(cache.cacheobjs[i].readCnt==1) 
        P(&cache.cacheobjs[i].wmutex); // i번째 캐시의 write lock 
    V(&cache.cacheobjs[i].rdcntmutex);
}

void readerAfter(int i){
    // readCnt 제어
    P(&cache.cacheobjs[i].rdcntmutex);
    // reader의 수 하나 감소
    cache.cacheobjs[i].readCnt--; //971쪽 readcnt-- 의미
    // 마지막 reader가 떠나면 (readCnt: 1 -> 0) writing lock을 풀어준다: V(&w)
    if(cache.cacheobjs[i].readCnt==0)
        V(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

/* writing 과정(wmutex semaphore)
 *
 * writePre()
 * actual writing...
 * writeAfter()
 * 
 */
void writePre(int i){ //write lock
    P(&cache.cacheobjs[i].wmutex);
}

void writeAfter(int i){ //write unlock
    V(&cache.cacheobjs[i].wmutex);
}

/* 캐시들(10개)에 uri가 있는지 확인하는 함수 */
int cache_find(char *url){
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){
        // 캐시 읽을 때 보호조치: readerPre() ... reading ... readerAfter()
        readerPre(i);
        if((cache.cacheobjs[i].isEmpty==0) && (strcmp(url, cache.cacheobjs[i].cache_url)==0)) break;
        // i번째 캐시가 비어있지 않고(1이면 비어있음) 
        readerAfter(i);
    }
    if(i>=CACHE_OBJS_COUNT) return -1; /*cannot find url in the cache*/
    return i;
}

/* 축출할 캐시 찾는 함수 */
int cache_eviction(){
    // 비었거나, 가장 작은 LRU값을 가진 캐시 번호를 찾는 함수 
    int min = LRU_MAGIC_NUMBER;
    int minindex = 0;
    int i;
    for(i=0; i<CACHE_OBJS_COUNT; i++)
    {
        // 캐시 읽을 때 보호조치: readerPre() ... reading ... readerAfter()
        readerPre(i);
        if(cache.cacheobjs[i].isEmpty == 1){ /* 비어있는 캐시 찾으면 탐색 종료 */
            minindex = i;
            readerAfter(i);
            break;
        }
        if(cache.cacheobjs[i].LRU< min){    /* 비어있지 않다면 가장 낮은 LRU값을 가진 캐시 반환*/
            minindex = i;
        }
        readerAfter(i);
    }

    return minindex;
}

/*update the LRU number(LRU--) except the new cache one*/
// 현재 캐시, 혹은 빈 캐시를 제외한 모든 캐시의 LRU--
void cache_LRU(int index){
    int i;
    for(i=0; i<index; i++)    {
        writePre(i);
        // if(cache.cacheobjs[i].isEmpty==0 && i!=index){ //비어 있지 않고, 자기 자신이 아닌 것..
        if(cache.cacheobjs[i].isEmpty==0 && i!=index){
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
    i++;
    for(i; i<CACHE_OBJS_COUNT; i++)    {
        writePre(i);
        if(cache.cacheobjs[i].isEmpty==0){
            cache.cacheobjs[i].LRU—;
        }
        writeAfter(i);
    }
}

/*cache the uri and content in cache*/
// 축출할 캐시를 고르고, 그 캐시에 새로운 uri 정보 저장
void cache_uri(char *uri,char *buf){

    int i = cache_eviction();

    writePre(i);/*writer P*/

    strcpy(cache.cacheobjs[i].cache_obj,buf);
    strcpy(cache.cacheobjs[i].cache_url,uri);
    cache.cacheobjs[i].isEmpty = 0;
    cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER;
    cache_LRU(i);

    writeAfter(i);/*writer V*/
}