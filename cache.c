#include "cache.h"

/* 새로운 캐시 객체 생성 */
void create_cache_obj(const char *uri, const char *data, long size)
{
    cache_obj *obj = malloc(sizeof(cache_obj));
    obj->uri = malloc(strlen(uri) + 1);
    obj->data = malloc(size);
    strcpy(obj->uri, uri);
    memcpy(obj->data, data, size); 
    obj->size = size;
    obj->prev = NULL;
    obj->next = NULL;
    insert_cache_obj(obj); // 새로운 캐시 객체 생성하면서 바로 연결리스트에 추가
}

/* 캐시에 클라이언트가 요청한 uri가 있는지 확인 */
cache_obj *find_cache_obj(char *uri)
{
    cache_obj *cur = head;
    if (head == NULL)
        return NULL;

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
  free(obj);
}

void insert_cache_obj(cache_obj *obj)
{
  while (cache_size + obj->size > MAX_CACHE_SIZE) {
    free_cache_obj(tail);
  }

  obj->next = head; // 항상 head 위치에 삽입
  if (head != NULL)
    head->prev = obj;
  head = obj;
  cache_size += obj->size;
}

void cache_hit(int clientfd, cache_obj *obj)
{
    printf("cache hit\n");
    delete_cache_obj(obj); // 최근에 참조한 객체가 head가 되도록 먼저 캐시 리스트에서 삭제
    printf("=======cache data=======\n%s\n========================\n", obj->data);
    rio_writen(clientfd, obj->data, obj->size); // 캐시 객체를 클라이언트로 전송
    insert_cache_obj(obj); // 참조 객체를 head에 추가
}

void cache_miss(int clientfd, char *uri, char *hostname, char *port, char *request_buf)
{
    /* 엔드 서버로 클라이언트의 요청 보내기 */
    int serverfd = Open_clientfd(hostname, port);
    char response_buf[MAX_OBJECT_SIZE];
    rio_t response_rio;

    printf("%s\n", request_buf);
    rio_writen(serverfd, request_buf, strlen(request_buf));
    Rio_readinitb(&response_rio, serverfd);

    /* 응답 헤더, 바디 모두 캐시 데이터에 저장 */
    ssize_t response_size = Rio_readnb(&response_rio, response_buf, MAX_OBJECT_SIZE);
    rio_writen(clientfd, response_buf, response_size);

    /* 캐시 용량 체크하고 케시에 저장 */
    if ((response_size < MAX_OBJECT_SIZE) && (cache_size + response_size > MAX_CACHE_SIZE)) {
        while (cache_size + response_size > MAX_CACHE_SIZE)
            free_cache_obj(tail); 
    }
    printf("=======response=======\n%s\n======================\n", response_buf);
    create_cache_obj(uri, response_buf, response_size); // 캐시 객체 생성
    Close(serverfd);
}
