#pragma once

#include <stdio.h>
#include <string.h>

#if OsHasFlags(OS_FLAGS_POSIX)
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

function void slice_grow(usize *len, usize *cap, Mem_Base *mb, void **items, usize item_size);

#define DefSlice(t) struct Glue(Slice_, t) { usize len; usize cap; Mem_Base *mb; t *items; }
#define Slice(t) struct Glue(Slice_, t)
#define SliceNew(t, membase) (Slice(t)){ .len = 0, .cap = 0, .mb = (membase), .items = NULL }
#define $(s, i) ((i >= s.len) ? (s.items = Unreachable("index out of bounds")) : &s.items[i])
#define SliceAppend(sp, v) do {                                                                  \
    if ((sp)->len + 1 > (sp)->cap)                                                               \
      slice_grow(&(sp)->len, &(sp)->cap, (sp)->mb, (void **)&(sp)->items, sizeof(*(sp)->items)); \
    (sp)->items[(sp)->len++] = v;                                                                \
  } while (false)
#define SliceLen(s) s.len

struct RpcServer;

DefSlice(u8);
typedef struct RpcResponse {
  u8        code;
  Slice(u8) data;
} RpcResponse;

typedef RpcResponse RpcHandlerFunc(struct RpcServer *srv, Slice(u8) data, void *ctx);

typedef struct RpcHandler {
  u64 uid;
  RpcHandlerFunc *f;
  void *ctx;
} RpcHandler;
DefSlice(RpcHandler);

typedef struct RpcServer {
  Mem_Base *mb;

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
  mbedtls_net_context  client_fd;
  RpcServer           *server;
} RpcClient;

function int init_rpc_server(RpcServer *srv);
function int run_rpc_server(RpcServer *srv);

function RpcHandler rpc_handler_new(u64 uid, RpcHandlerFunc *f, void *ctx);
function void       rpc_server_reg_handler(RpcServer *srv, RpcHandler hdl);
