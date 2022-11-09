#include "csapp.h"
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, char *port, char *path);

void echo(int connfd);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *host_header = "Host : %s\r\n"; // 호스트 이름
static const char *connection_header = "Connection: close\r\n"; // connection 헤더
static const char *proxy_connection_header = "Proxy-Connection: close\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

/*
main(argc, argv)
tiny 함수를 실행할떼 ./tiny 8000 이런식으로 실행한다.
이때 단어의 수가 argc에 들어간다. 여기서는 argc는 2이다.
argv는 배열이고 위의 예시를 통해 알아보면 
argv[0] = ./tiny argv[1] = 8000 이렇게 된다.
*/
int main(int argc, char **argv) {
  printf("%s", user_agent_hdr);
  
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 듣기 소켓 오픈
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // 연결 요청 접수
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // 트랜잭션 수행
    // echo(connfd);
    Close(connfd);  // 자신 쪽의 연결 끝을 닫음
  }

  return 0;
}



/*
 doit(fd)
 한 개의 HTTP 트랜잭션을 처리한다.
 클라이언트의 요청 라인을 확인해서 정적, 동적 컨텐츠를 확인하고 돌려준다.
*/
void doit(int fd)
{
  int clientfd;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  // 요청 라인을 읽고 분석
  Rio_readlineb(&rio, buf, MAXLINE); 
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  parse_uri(uri, hostname, port, filename);

  clientfd = Open_clientfd(hostname, port); // 서버와 프록시랑 연결해주는 식별자(서버의 ip와 포트를 인자로 넣음)
  Rio_readinitb(&rio, clientfd); // 공유하는 버퍼 주소 초기 설정(공유하는 공간 만듦)
  Rio_writen(clientfd, buf, strlen(buf)); // 버퍼에 있는 내용을 클라이언트 식별자에 입력
  Close(clientfd); 
}

void make_header(char *buf, char *hostname, char *path, rio_t *rio)
{
  char rio_buf[MAXLINE], r_host_header[MAXLINE];
  sprintf(buf, "GET %s HTTP/1.0", path);
  while(Rio_readlineb(rio, rio_buf, MAXLINE) > 0){
    if(strcmp(rio_buf, "\r\n"==0)){
      break;
    }
    if(!strncasecmp(rio_buf, host_key, strlen(host_key))){
      strcpy(r_host_header, rio_buf);
      continue;
    }
    /*
    static const char *connection_key = "Connection";
    static const char *proxy_connection_key = "Proxy-Connection";
    static const char *user_agent_key = "User-Agent";
    */
    if(!strncasecmp(rio_buf, connection_key, strlen(connection_key)) || !strncasecmp(rio_buf, proxy_connection_key, strlen(proxy_connection_key)) || !strncasecmp(rio_buf, user_agent_key, strlen(user_agent_key))){
      strcat()
    }
  }
}

/* 
read_requesthdrs
tiny에서는 요청 헤더 내의 어떤 정보도 사용하지 않는다.
그래서 요청 헤더를 읽고 무시한다.
*/
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  // 요청 헤더를 종료하는 빈 텍스트 줄이 있는지 확인
  while(strcmp(buf, "\r\n")) { // strcmp : 두 개의 문자열이 같은지 비교
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
parse_uri
정적 컨텐츠를 위한 홈 디렉토리를 설정하고(tiny에서는 홈 디렉토리가 자신의 현재 디렉토리로 설정됨)
실행파일의 홈디렉토리를 설정한다(tiny에서는 /cgi-bin이 홈 디렉토리로 설정됨)
*/
int parse_uri(char *uri, char *hostname, char *port, char *filename)
{
  char *p;
  char arg1[MAXLINE], arg2[MAXLINE];

  // http://11.111.11.11:5000/home.html

  p = strchr(uri, '/');
  
  strcpy(arg1, p+2); // agr1 = 11.111.11.11:5000/home.html

  printf("%s\n", arg1);
  
  if(strstr(arg1, ":")){
    p = strchr(arg1, ':');
    *p = '\0';
    strcpy(hostname, arg1); // hostname = 11.111.11.11
    strcpy(arg2, p+1); // arg2 = 5000/home.html
    printf("%s\n", hostname);
    printf("%s\n", arg2);
    

    p = strchr(arg2, '/'); 
    *p = '\0';
    strcpy(port, arg2); // port = 5000

    strcpy(filename, p+1);

    printf("%s\n", port);
    printf("%s\n", filename);
  }
  else { // http://11.111.11.11/home.html
    // agr1 = 11.111.11.11/home.html
    p = strchr(arg1, '/');
    *p = '\0';
    strcpy(hostname, arg1); // hostname = 11.111.11.11
    strcpy(filename, p+1); // filename = home.html
    printf("%s\n", hostname);
    printf("%s\n", filename);
  }

  return 0;

}

