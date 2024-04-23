#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void *thread(void *vargp);
void doit(int clientfd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void update_hdr(char *request_hdr, char *method, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd, *clientfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  signal(SIGPIPE, SIG_IGN); // broken pipe 에러 해결용 코드 -프로세스 전체에 대한 시그널 핸들러 설정

  /* 클라이언트 요청 받기 */
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    clientfdp = malloc(sizeof(int));
    *clientfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, clientfdp);
  }
}

void *thread(void *vargp)
{
  int clientfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(clientfd);
  Close(clientfd);
  return NULL;
}

void doit(int clientfd)
{
  char request_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t request_rio;
  cache_obj *cache_check;

  /* 클라이언트의 요청 읽기 */
  Rio_readinitb(&request_rio, clientfd);
  Rio_readlineb(&request_rio, request_buf, MAXLINE);
  printf("Request header: %s\n", request_buf);
  sscanf(request_buf, "%s %s", method, uri); // 요청 메소드, uri 읽기
  
  if (!strcasecmp(uri, "/favicon.ico")) 
    return;

  /* 캐시 hit 확인 */
  if (cache_check = find_cache_obj(uri)) {
    cache_hit(clientfd, cache_check);
    return;
  }

  /* 캐시 miss */
  parse_uri(uri, hostname, port, path); // uri에서 hostname, port, path 파싱 
  update_hdr(request_buf, method, path); // 클라이언트 요청 헤더 수정

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(clientfd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }

  cache_miss(clientfd, uri, hostname, port, request_buf);
}

// uri: `http://hostname:port/path` 또는 `http://hostname/path`
int parse_uri(char *uri, char *hostname, char *port, char *path) 
{
  char uri_copy[MAXLINE];
  strcpy(uri_copy, uri);
  char *hostname_ptr = strstr(uri_copy, "//") != NULL ? strstr(uri_copy, "//") + 2 : uri_copy + 1;
  char *port_ptr = strstr(hostname_ptr, ":");
  char *path_ptr = strstr(hostname_ptr, "/");
  if (path_ptr > 0) {
    *path_ptr = '\0';
    strcpy(path, path_ptr+1);
  }
  if (port_ptr > 0) {
    *port_ptr = '\0';
    strcpy(port, port_ptr + 1);
  }
  strcpy(hostname, hostname_ptr);
  return 0;
}

void update_hdr(char *request_hdr, char *method, char *path)
{
  sprintf(request_hdr, "%s /%s %s\r\n", method, path, "HTTP/1.0");
  sprintf(request_hdr, "%sConnection: close\r\n", request_hdr);
  sprintf(request_hdr, "%sProxy-Connection: close\r\n", request_hdr);
  sprintf(request_hdr, "%s%s\r\n", request_hdr, user_agent_hdr); 
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}
