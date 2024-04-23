#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* 캐시 구현 */
typedef struct _cache_obj {
  char *uri;
  unsigned char *data; // 이미지 데이터 처리를 위해 바이트 단위로 저장
  char *content_type;
  struct _cache_obj *prev;
  struct _cache_obj *next;
  long size;
} cache_obj;

static int cache_size = 0; // 캐시 크기는 최대 1MB
static cache_obj *head = NULL;
static cache_obj *tail = NULL;

void *thread(void *vargp);
void doit(int clientfd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void read_requesthdrs(rio_t *rp);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
long parse_content_length(const char *str);
void create_cache_obj(const char *uri, const char *data, long size, const char *content_type);  // 캐시 객체 생성
cache_obj *find_cache_obj(char *uri);        // 캐시 객체 연결리스트에서 uri 응답 객체 찾기
void delete_cache_obj(cache_obj *obj);       // 캐시 객체 연결리스트에서 객체 삭제
void free_cache_obj(cache_obj *obj);         // 캐시 객체 완전히 삭제
void insert_cache_obj(cache_obj *obj);       // 캐시 객체 연결리스트에 객체 추가


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
    // doit(clientfd);   // line:netp:tiny:doit
    // Close(clientfd);  // line:netp:tiny:close
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
  int serverfd;
  char request_buf[MAXLINE], response_buf[MAX_OBJECT_SIZE], method[MAXLINE], uri[MAXLINE], hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t request_rio, response_rio;
  cache_obj *cache_check;

  /* 클라이언트의 요청 읽기 */
  Rio_readinitb(&request_rio, clientfd);
  Rio_readlineb(&request_rio, request_buf, MAXLINE);
  printf("Request header: %s\n", request_buf);
  /* 요청 메소드, uri 읽기 */
  sscanf(request_buf, "%s %s", method, uri);
  
  if (!strcasecmp(uri, "/favicon.ico")) 
    return;


  printf("uri: %s\n", uri);

  /* 캐시 확인 */
  if (cache_check = find_cache_obj(uri)) {
    char buf[MAXBUF];
    printf("cache hit\n");
    /* 캐시에서 꺼내서 클라이언트로 보내주기 */
    delete_cache_obj(cache_check); // head에 다시 넣어주려고 먼저 삭제함
    printf("============cache data=========\n%s\n============\n", cache_check->data);
    // 헤더 만들어주기
    sprintf(buf, "HTTP/1.0 200 Ok\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %ld\r\n", buf, cache_check->size);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, cache_check->content_type);
    printf("cache response header:\n%s", buf);
    rio_writen(clientfd, buf, strlen(buf)); // 헤더 전송
    rio_writen(clientfd, cache_check->data, cache_check->size); // 바디 전송
    // 클라이언트로 보내주고 나서 hit한 캐시 객체를 다시 캐시 head에 넣어주기
    insert_cache_obj(cache_check);
    return;
  }

  parse_uri(uri, hostname, port, path); // uri에서 hostname, port, path 파싱 
  /* 서버에 요청 전달하려고 요청 헤더 수정하는 부분 */
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
  rio_writen(serverfd, request_buf, strlen(request_buf));
  Rio_readinitb(&response_rio, serverfd);

  ssize_t n;
  long content_length;
  char *content_type = malloc(MAXLINE);
  /* 엔드 서버의 응답 헤더를 클라이언트로 보내기 */
  while ((n = Rio_readlineb(&response_rio, response_buf, MAX_OBJECT_SIZE)) > 0) {
    rio_writen(clientfd, response_buf, n);
    if (strstr(response_buf, "Content-length")) 
      content_length = parse_content_length(&response_buf); // 응답 body의 크기
    if (strstr(response_buf, "Content-type"))  {
      char *type_ptr = strstr(response_buf, "Content-type") + strlen("Content-type: ");
      strcpy(content_type, type_ptr);
    }
    if (!strcmp(response_buf, "\r\n"))
      break;
  }
  printf("=======content-type: %s\n", content_type);  

  char *reponse_body = malloc(sizeof(int) * content_length);
  /* 응답 보내기 */
  n = Rio_readnb(&response_rio, reponse_body, content_length);
  printf("======cache miss response body===========\n");
  printf("%s\n", reponse_body);
  printf("=================\n");
  rio_writen(clientfd, reponse_body, content_length);

  /* 캐시에 응답 body 저장 */
  /* 캐시 max 사이즈 체크하고 저장하기 */
  if ((content_length < MAX_OBJECT_SIZE) && (cache_size + content_length > MAX_CACHE_SIZE)) {
    while (cache_size + content_length > MAX_CACHE_SIZE)
      free_cache_obj(tail); // 한개가 아니고 여러개 삭제해야할수도잇음
  }
  printf("-----uri: %s\n", uri);
  printf("-----content-type: %s\n", content_type);
  printf("===response_body:\n%s\n===", reponse_body);
  create_cache_obj(uri, reponse_body, content_length, content_type);
  Close(serverfd);
  free(reponse_body);
  free(content_type);
}

// 문자열에서 숫자를 파싱하여 long 변수에 담는 함수
long parse_content_length(const char *str) {
  long content_length = 0;
  const char *ptr = str;
  while (*ptr != '\0') {
    if (isdigit(*ptr)) {
      content_length = content_length * 10 + (*ptr - '0');
    }
    ptr++;
  }
  return content_length;
}

// uri: `http://hostname:port/path` 또는 `http://hostname/path`
int parse_uri(char *uri, char *hostname, char *port, char *path) 
{
  printf("---parse_uri: %s\n", uri);
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
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}

/*
* 캐시 관련 함수
*/
/* 새로운 캐시 객체 생성 */
void create_cache_obj(const char *uri, const char *data, long size, const char *content_type)
{
  cache_obj *obj = malloc(sizeof(cache_obj));
  obj->uri = malloc(strlen(uri) + 1);
  obj->data = malloc(size);
  obj->content_type = malloc(strlen(content_type) + 1);
  strcpy(obj->uri, uri);
  memcpy(obj->data, data, size); // 데이터 복사
  strcpy(obj->content_type, content_type);
  obj->size = size;
  obj->prev = NULL;
  obj->next = NULL;
  insert_cache_obj(obj); // 새로운 캐시 객체 생성하면서 바로 연결리스트에 넣어주기
}

/* 캐시에 클라이언트가 요청한 uri가 있는지 확인 */
cache_obj *find_cache_obj(char *uri)
{
  if (head == NULL)
    return NULL;

  cache_obj *cur = head;
  while (cur != NULL) {
    if (!strcasecmp(cur->uri, uri))
      return cur;
    cur = cur->next;
  }
  return NULL;
}

void delete_cache_obj(cache_obj *obj)
{
  if (obj == tail) {
    obj->prev->next = NULL;
    tail = obj->prev;
  }
  else if (obj == head) {
    obj->next->prev = NULL;
    head = obj->next;
    obj->next = NULL;
  }
  else { // 중간에 있는 캐시 삭제
    obj->prev->next = obj->next;
    if (obj->next != NULL)
      obj->next->prev = obj->prev;
  }
}

void free_cache_obj(cache_obj *obj)
{
  delete_cache_obj(obj);
  cache_size -= obj->size;
  free(obj->uri);
  free(obj->data);
  free(obj->content_type);
  free(obj);
}

void insert_cache_obj(cache_obj *obj)
{
  while (cache_size + obj->size > MAX_CACHE_SIZE) {
    free_cache_obj(tail);
  }

  obj->next = head;
  if (head != NULL)
    head->prev = obj;
  head = obj;
  cache_size += obj->size;
}
