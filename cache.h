#ifndef __CACHE_H__
#define __CACHE_H__

#include <stdlib.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 캐시 객체 */
typedef struct _cache_obj {
  char *uri;
  unsigned char *data; // 이미지 데이터 처리를 위해 바이트 단위로 저장
  struct _cache_obj *prev;
  struct _cache_obj *next;
  long size;
} cache_obj;

static int cache_size = 0; // 캐시 크기는 최대 1MB
static cache_obj *head = NULL;
static cache_obj *tail = NULL;

void create_cache_obj(const char *uri, const char *data, long size);  // 캐시 객체 생성
cache_obj *find_cache_obj(char *uri);        // 캐시 객체 연결리스트에서 uri 응답 객체 찾기
void delete_cache_obj(cache_obj *obj);       // 캐시 객체 연결리스트에서 객체 삭제
void free_cache_obj(cache_obj *obj);         // 캐시 객체 메모리 해제
void insert_cache_obj(cache_obj *obj);       // 캐시 객체 연결리스트에 객체 추가
void cache_hit(int fd, cache_obj *obj);
void cache_miss(int clientfd, char *uri, char *hostname, char *port, char *request_buf);

#endif