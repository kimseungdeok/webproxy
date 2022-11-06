/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
/*
main(argc, argv)
tiny 함수를 실행할떼 ./tiny 8000 이런식으로 실행한다.
이때 단어의 수가 argc에 들어간다. 여기서는 argc는 2이다.
argv는 배열이고 위의 예시를 통해 알아보면 
argv[0] = ./tiny 
argv[1] = 8000
이렇게 된다.
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
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // 연결 요청 접수
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // 트랜잭션 수행
    Close(connfd);  // 자신 쪽의 연결 끝을 닫음
  }
}

/*
 doit(fd) : 한 개의 HTTP 트랜잭션을 처리한다.
*/
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  // 요청 라인을 읽고 분석
  Rio_readlineb(&rio, buf, MAXLINE); 
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  // tiny는 get 메소드만 지원, 
  // 그래서 클라이언트가 다른 메소드(POST)를 요청하면 에러 메시지를 보내고
  // main 루틴으로 돌아오고 그 후에 연결을 닫고 다음 연결 요청을 기다린다.
  if(strcasecmp(method, "GET")){
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); // 다른 요청 헤더를 무시한다.

  
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename,&sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
  if(is_static) { /* 정적 컨텐츠인지 아닌지 확인 */
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){ /* 보통 파일이라는 것과 읽기 권한을 가지고 있는지를 검증*/
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size); // 정적 컨텐츠를 클라이언트에게 제공한다.
  }
  else { // 동적 컨텐츠에 대한 것이라면
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { // 동적 컨텐츠가 실행 가능한지 검증
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // 동적 컨텐츠를 제공
  }
}
/*
clienterror
오류를 체크하고 클라이언트에게 보고하는 함수
HTTP 응답을 응답 라인에 적절한 상태 코그와 상태 메시지와 함께 클라이언트에 보내며 
브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML파일도 함께 보낸다.
*/
void clienterror(int fd, char *cause, char *errnum, 
                  char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""fffff"">\r\n", body);
  sprintf(body, "%s%s:%s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s : %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, buf, strlen(body));

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
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if(!strstr(uri, "cgi-bin")){
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if(uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else {
    ptr = index(uri, '?');
    if(ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else  
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}
/*
serve_static
지역 파일의 내용을 포함하고 있는 본체를 갖는 HTTP응답을 보낸다.
먼저 파일 이름의 접미어 부분을 검사해서 파일 타입을 결정하고 
클라이언트에 응답 중과 응답 헤더를 보낸다.
*/
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sConnect-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  srcfd = Open(filename, O_RDONLY, 0);

  /* 
  void * mmap(void *start, size_t length, int prot, int flags, int flides, off_t offset);
  offset을 시작으로 length바이트 만큼 start(보통 0을 지정, 그 주소를 사용하면 좋겠다 정도)주소로 대응시키도록 한다.
  */
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); 
  srcp = (char *) malloc(filesize);
  // 이 구문에서 rio_readn 함수는 식별자 srcfd의 현재 파일 위치에서 메모리 위치 srcp로 최대 filesize 바이트를 전송한다.
  Rio_readn(srcfd, srcp, filesize); 
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
  free(srcp);

}

/*
get_filetype
파일의 컨텐츠 타입을 확인하는 함수
*/
void get_filetype(char *filename, char *filetype)
{
  if(strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if(strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if(strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if(strstr(filename, ".mpg"))
    strcpy(filetype, "image/mpg");
  else if(strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}
/*
serve_dynamic
동적 컨텐츠를 자식의 컨텍스트에서 실행할수있도록 해주는 함수이다.
*/
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if(Fork() == 0){ // 새로운 자식 프로세스를 fork함
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, emptylist, environ);
  }
  Wait(NULL);
}