#pragma once

#include <stdio.h>
#include <string.h>

#if OsHasFlags(OsFlags_Posix)
# include <arpa/inet.h>
# include <netdb.h>
# include <signal.h>
# include <sys/socket.h>
# include <unistd.h>
#endif

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>
#include <mbedtls/ssl_cache.h>

struct RpcServer;

typedef struct RpcResponse {
  u8  code;
  u8 *data;
} RpcResponse;

typedef RpcResponse RpcHandlerFunc(struct RpcServer *srv, u64 uid, const u8 *data);

typedef struct RpcHandler {
  RpcHandlerFunc *f;
  void *ctx;
} RpcHandler;

typedef struct SliceHeader {
  usize len;
  usize cap;
  Mem_Base *mb;
} SliceHeader;

#define Slice(t) struct { SliceHeader header; t *items; }
#define SliceNew(t, mb) (Slice(t)){ .header = { .len = 0, .cap = 0, .mb = mb }, .items = NULL }
#define $(s, i) ((i >= s.header.len) ? (s.items = Unreachable("index out of bounds")) : &s.items[i])
#define SliceAppend(sp, v) do {                                                \
    SliceHeader *h = &(sp)->header;                                            \
    if (h->len + 1 > h->cap) {                                                 \
      void *new_items = mem_reserve(h->mb, sizeof(*(sp)->items) * h->cap * 2); \
      mem_commit(h->mb, new_items, sizeof(*(sp)->items) * h->cap * 2);         \
      memmove(new_items, (sp)->items, sizeof(*(sp)->items) * h->len);          \
      mem_decommit(h->mb, (sp)->items, sizeof(*(sp)->items) * h->cap);         \
      mem_release(h->mb, (sp)->items, sizeof(*(sp)->items) * h->cap);          \
      (sp)->items = new_items;                                                 \
      h->cap *= 2;                                                             \
    }                                                                          \
    (sp)->items[h->len++] = v;                                                 \
  } while (false);

typedef struct RpcServer {
  mbedtls_x509_crt          cert;
  mbedtls_pk_context        pk;
  mbedtls_net_context       listen_fd;
  mbedtls_ctr_drbg_context  ctr_drbg;
  mbedtls_entropy_context   entropy;
  mbedtls_ssl_config        conf;
  mbedtls_ssl_cache_context cache;

  Slice(RpcHandler) handlers;
} RpcServer;

typedef struct RpcClient {
  mbedtls_net_context   client_fd;
  RpcServer            *server;
} RpcClient;

function int init_rpc_server(RpcServer *srv);
function int run_rpc_server(RpcServer *srv);

function RpcHandler
rpc_handler_new(RpcHandlerFunc *f, void *ctx);
