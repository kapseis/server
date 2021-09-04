#pragma once

#include "base.h"
#if INTEL_INTRINSICS_AVAILABLE
# include <immintrin.h>
#endif

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

function usize slice_next_cap(usize want_len);
function void  slice_grow(usize *len, usize *cap, Mem_Base *mb, void **items, usize item_size);
function void  slice_grow_for(usize new_len, usize *len, usize *cap, Mem_Base *mb, void **items, usize item_size);
function void  slice_destroy(usize cap, Mem_Base *mb, void *items, usize item_size);

#define DefSlice(t) struct Glue(Slice_, t) { usize len; usize cap; Mem_Base *mb; t *items; }
#define Slice(t) struct Glue(Slice_, t)
#define SliceNew(t, membase) (Slice(t)){ .len = 0, .cap = 0, .mb = (membase), .items = NULL }
#define SliceNewWithCap(t, membase, cap) (Slice(t)){ .len = 0, .cap = (cap), .mb = (membase), .items = mem_reserve_commit((membase), cap * sizeof(t)) }
#define SliceDestroy(s) slice_destroy((s).cap, (s).mb, (s).items, sizeof(*(s).items))
#define $(s, i) (((i) >= (s).len) ? ((s).items = Unreachable("index out of bounds")) : &(s).items[i])
#define SliceAppend(sp, v) do {                                                                  \
    if ((sp)->len + 1 > (sp)->cap)                                                               \
      slice_grow(&(sp)->len, &(sp)->cap, (sp)->mb, (void **)&(sp)->items, sizeof(*(sp)->items)); \
    (sp)->items[(sp)->len++] = v;                                                                \
  } while (false)
#define SliceExtend(sp, s) do { \
    StaticAssert(sizeof(*(sp)->items) == sizeof(*(s).items), "SliceExtend must be provided with two slices, with items of equal size"); \
    if ((sp)->len + (s).len > (sp)->cap) \
      slice_grow_for((sp)->len + (s).len, &(sp)->len, &(sp)->cap, (sp)->mb, (void **)&(sp)->items, sizeof(*(sp)->items)); \
    memmove(&(sp)->items[(sp)->len], (s).items, sizeof(*(sp)->items) * (sp)->cap); \
  } while (false)
#define SliceLen(s) (s).len

struct RpcServer;

DefSlice(u8);
typedef struct RpcResponse {
  u64 client_id;
  u64 request_id;
  u8  code;
  Slice(u8) data;
} RpcResponse;

typedef struct RpcRequest {
  u64 uid;
  u64 client_id;
  u64 request_id;
  Slice(u8) data;
} RpcRequest;

typedef void RpcHandlerFunc(struct RpcServer *srv, RpcRequest req, void *ctx);

typedef struct RpcHandler {
  u64 uid;
  RpcHandlerFunc *f;
  void *ctx;
} RpcHandler;
DefSlice(RpcHandler);

struct RpcServer;

typedef enum RpcClientReadState {
  RpcClientReadState_Start,
  RpcClientReadState_Body,
} RpcClientReadState;

typedef struct RpcClient {
  u64 id;
  mbedtls_net_context fd;
  struct RpcServer *server;

  RpcClientReadState rstate;
  Slice(u8) rbuf;
  Slice(u8) wbuf;
  usize wbufi;
} RpcClient;
DefSlice(RpcClient);

typedef struct RpcServer {
  Mem_Base *mb;

  mbedtls_x509_crt          cert;
  mbedtls_pk_context        pk;
  mbedtls_net_context       listen_fd;
  mbedtls_ctr_drbg_context  ctr_drbg;
  mbedtls_entropy_context   entropy;
  mbedtls_ssl_config        conf;
  mbedtls_ssl_cache_context cache;

  Slice(RpcClient)  clients;
  Slice(RpcHandler) handlers;
} RpcServer;

function int init_rpc_server(RpcServer *srv);
function int run_rpc_server(RpcServer *srv);

function RpcHandler rpc_handler_new(u64 uid, RpcHandlerFunc *f, void *ctx);
function void       rpc_server_reg_handler(RpcServer *srv, RpcHandler hdl);
