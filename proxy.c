#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int clientfd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void read_requesthdrs(rio_t *rp);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// static const int is_local_test = 1; // 외부에서 테스트 

int main(int argc, char **argv) {
  int listenfd, clientfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 클라이언트 요청 받기 */
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(clientfd);   // line:netp:tiny:doit
    Close(clientfd);  // line:netp:tiny:close
  }
}

void doit(int clientfd)
{
  int serverfd;
  char request_buf[MAXLINE], response_buf[MAX_OBJECT_SIZE], method[MAXLINE], uri[MAXLINE], hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t request_rio, response_rio;

  /* 클라이언트의 요청 읽기 */
  Rio_readinitb(&request_rio, clientfd);
  Rio_readlineb(&request_rio, request_buf, MAXLINE);
  printf("Request header: %s\n", request_buf);
  /* 요청 메소드, uri 읽기 */
  sscanf(request_buf, "%s %s", method, uri);
  parse_uri(uri, hostname, port, path); // uri에서 hostname, port, path 파싱

  printf("uri: %s\n", uri);

  sprintf(request_buf, "%s /%s %s\r\n", method, path, "HTTP/1.0");
  printf("%s\n", request_buf);
  sprintf(request_buf, "%sConnection: close\r\n", request_buf);
  sprintf(request_buf, "%sProxy-Connection: close\r\n", request_buf);
  sprintf(request_buf, "%s%s\r\n", request_buf, user_agent_hdr);

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(clientfd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }

  /* 엔드 서버로 클라이언트의 요청 보내기 */
  serverfd = Open_clientfd(hostname, port);
  printf("%s\n", request_buf);
  Rio_writen(serverfd, request_buf, strlen(request_buf));
  
  ssize_t n;
  n = Rio_readn(serverfd,response_buf,MAX_OBJECT_SIZE); // 응답 받는건 OBJECT_SIZE 만큼
  Rio_writen(clientfd,response_buf,n);
  Close(serverfd);
}

// uri: `http://hostname:port/path` 또는 `http://hostname/path`
int parse_uri(char *uri, char *hostname, char *port, char *path) 
{
  printf("---parse_uri: %s\n", uri);
  char *hostname_ptr = strstr(uri, "//") != NULL ? strstr(uri, "//") + 2 : uri + 1;
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
  printf("---parse_uri host: %s, port: %s, path: %s\n", hostname, port, path);
  
  return 0;
}

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
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
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}