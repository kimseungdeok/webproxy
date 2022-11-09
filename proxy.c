#include "csapp.h"
#include <stdio.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *host_header = "Host: %s\r\n"; // 호스트 이름
static const char *connection_header = "Connection: close\r\n"; // connection 헤더
static const char *proxy_connection_header = "Proxy-Connection: close\r\n";

static const char *user_agent_key = "User_Agent";
static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy_Connection";

void doit(int fd);
void make_header(char *header, char *hostname, char *path, rio_t *rio);
int parse_uri(char *uri, char *hostname, char *port, char *filename);


/*
main(argc, argv)
tiny 함수를 실행할떼 ./tiny 8000 이런식으로 실행한다.
이때 단어의 수가 argc에 들어간다. 여기서는 argc는 2이다.
argv는 배열이고 위의 예시를 통해 알아보면 
argv[0] = ./tiny argv[1] = 8000 이렇게 된다.
*/
int main(int argc, char **argv) {
  
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
    connfd = Accept(listenfd, (SA *)&clientaddr,&clientlen);  // 연결 요청 접수
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // 트랜잭션 수행
    Close(connfd);  // 자신 쪽의 연결 끝을 닫음
  }
  print("%s", user_agent_hdr);
  return 0;
}



/*
 doit(fd)
 한 개의 HTTP 트랜잭션을 처리한다.
 클라이언트의 요청 라인을 확인해서 정적, 동적 컨텐츠를 확인하고 돌려준다.
*/
void doit(int fd)
{
  int clientfd; // 클라이언트 파일 디스크립터
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], server_header[MAXLINE];
  char filename[MAXLINE], hostname[MAXLINE], port[MAXLINE];
  rio_t rio, server_rio;
  size_t n;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  // 요청 라인을 읽고 분석
  Rio_readlineb(&rio, buf, MAXLINE); 
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  parse_uri(uri, hostname, port, filename);

  make_header(server_header, hostname, filename, &rio);

  clientfd = Open_clientfd(hostname, port); // 서버와 프록시랑 연결해주는 식별자(서버의 ip와 포트를 인자로 넣음)
  Rio_readinitb(&server_rio, clientfd); // 공유하는 버퍼 주소 초기 설정(공유하는 공간 만듦)
  Rio_writen(clientfd, server_header, strlen(server_header)); // 버퍼에 있는 내용을 클라이언트 식별자에 입력

  while((n = Rio_readlineb(&server_rio, buf, MAXLINE))!=0)
  {
    printf("%s\n", buf);
    Rio_writen(fd, buf, n);
  }
  Close(clientfd); 
}

/*
 make_header : 필요한 header들을 만들고 묶음
*/
void make_header(char *header, char *hostname, char *path, rio_t *rio)
{
  char buf[MAXLINE], r_host_header[MAXLINE], request_header[MAXLINE], other_header[MAXLINE];
  sprintf(request_header, "GET %s HTTP/1.0\r\n", path);
  while(Rio_readlineb(rio, buf, MAXLINE) > 0){
    if(strcmp(buf, "\r\n")==0){
      break;
    }
    if(!strncasecmp(buf, host_key, strlen(host_key))){
      strcpy(r_host_header, buf); // rio_buf를 host_key에 저장
      continue;
    }
    if(strncasecmp(buf, connection_key, strlen(connection_key)) 
        && strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) 
        && strncasecmp(buf, user_agent_key, strlen(user_agent_key))){
      strcat(other_header, buf);
    }
  }
  if(strlen(r_host_header) == 0)
  {
    sprintf(r_host_header, host_header, hostname);
  }

  sprintf(header, "%s%s%s%s%s%s%s", 
          request_header, r_host_header, connection_header,
          proxy_connection_header, user_agent_hdr, other_header, "\r\n");
  return;
}

/*
parse_uri
uri를 파싱해서 나누어서 처리해줌
*/
int parse_uri(char *uri, char *hostname, char *port, char *filename)
{
  char *p;
  char arg1[MAXLINE], arg2[MAXLINE];

  if(p = strchr(uri, '/'))
  {
    sscanf(p + 2, "%s", arg1);
  }
  else
  {
    strcpy(arg1, uri);
  }


  if (p = strchr(arg1, ':')){     
    *p = '\0';
    sscanf(arg1, "%s", hostname);       
    sscanf(p+1, "%s", arg2);            

    p = strchr(arg2, '/');
    *p = '\0';
    sscanf(arg2, "%s", port);           
    *p = '/';
    sscanf(p, "%s", filename);        

  }
  else{                           
    p = strchr(arg1, '/');
    *p = '\0';
    sscanf(arg1, "%s", hostname);
    *p = '/';
    sscanf(p, "%s", filename);

  }
  return;
}

