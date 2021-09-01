#include "base.h"

#include "rpc.h"

function s32
init_rpc_server(RpcServer *srv) {
  puts("Reading certificates");

  mbedtls_x509_crt_init(&srv->cert);
  int s = mbedtls_x509_crt_parse_file(&srv->cert, "cert.pem");
  if (s != 0) {
    perror("Failed to load cert.pem");
    return s;
  }

  mbedtls_pk_init(&srv->pk);
  s = mbedtls_pk_parse_keyfile(&srv->pk, "key.pem", "");
  if (s != 0) {
    perror("Failed to load key.pem");
    return s;
  }

  puts("Certificates read");

  mbedtls_ctr_drbg_init(&srv->ctr_drbg);
  mbedtls_entropy_init(&srv->entropy);
  const char *pers = "ssl_server";
  s = mbedtls_ctr_drbg_seed(&srv->ctr_drbg, mbedtls_entropy_func, &srv->entropy, (const u8 *)pers, strlen(pers));
  if (s != 0) {
    perror("Failed to seed random number generator");
    return s;
  }

  mbedtls_ssl_config_init(&srv->conf);
  s = mbedtls_ssl_config_defaults(&srv->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (s != 0) {
    perror("Failed to set up SSL config");
    return s;
  }

  mbedtls_ssl_conf_rng(&srv->conf, mbedtls_ctr_drbg_random, &srv->ctr_drbg);

  mbedtls_ssl_cache_init(&srv->cache);
  mbedtls_ssl_conf_session_cache(&srv->conf, &srv->cache, mbedtls_ssl_cache_get, mbedtls_ssl_cache_set);

  mbedtls_ssl_conf_ca_chain(&srv->conf, srv->cert.next, NULL);
  s = mbedtls_ssl_conf_own_cert(&srv->conf, &srv->cert, &srv->pk);
  if (s != 0) {
    perror("Failed to set up certificate");
    return s;
  }
  return 0;
}

function s32
run_rpc_server(RpcServer *srv) {
  puts("Listening");

  mbedtls_net_init(&srv->listen_fd);
  int s = mbedtls_net_bind(&srv->listen_fd, NULL, "4433", MBEDTLS_NET_PROTO_TCP);
  if (s != 0) {
    perror("Failed to bind");
    return s;
  }

  mbedtls_ssl_context ssl;
reset:
  mbedtls_ssl_init(&ssl);
  s = mbedtls_ssl_setup(&ssl, &srv->conf);
  if (s != 0) {
    perror("Failed to run SSL setup");
    return s;
  }

  mbedtls_net_context client_fd;
  mbedtls_net_init(&client_fd);

  mbedtls_net_free(&client_fd);
  mbedtls_ssl_session_reset(&ssl);

  puts("Accepting");
  s = mbedtls_net_accept(&srv->listen_fd, &client_fd, NULL, 0, NULL);
  if (s != 0) {
    perror("Failed to accept");
    return s;
  }
  puts("Accepted a connection");

  mbedtls_ssl_set_bio(&ssl, &client_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

  while ((s = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (s != MBEDTLS_ERR_SSL_WANT_READ && s != MBEDTLS_ERR_SSL_WANT_WRITE) {
      perror("SSL handshake failed");
      goto reset;
    }
  }

  puts("Handshake succeeded");

  u8 buf[4096];
  while (1) {
    memset(buf, 0, sizeof(buf));
    s = mbedtls_ssl_read(&ssl, buf, sizeof(buf)-1);

    if (s == MBEDTLS_ERR_SSL_WANT_READ || s == MBEDTLS_ERR_SSL_WANT_WRITE)
      continue;

    if (s <= 0) {
      switch (s) {
      case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
        puts("Connection was closed gracefully");
        break;
      case MBEDTLS_ERR_NET_CONN_RESET:
        puts("Connection was reset by peer");
        break;
      default:
        printf("Error: mbedtls_ssl_read returned -0x%x\n", (u32) -s);
        break;
      }
    }

    printf("%d bytes read\n\n%s", s, (char *)buf);

    if (s > 0)
      break;
  }

  return 0;
}

function RpcResponse
rpc_server_handle(RpcServer *srv, u64 uid, Slice(u8) data) {
  for (usize i = 0; i < SliceLen(srv->handlers); i++) {
    RpcHandler hdlr = srv->handlers.items[i];
    if (hdlr.uid == uid) return hdlr.f(srv, data, hdlr.ctx);
  }
  return (RpcResponse){
    .code = 5, /* not found */
    .data = SliceNew(u8, srv->mb),
  };
}

function void
rpc_server_reg_handler(RpcServer *srv, RpcHandler hdl) {
  SliceAppend(&srv->handlers, hdl);
  rpc_server_handle(srv, 1293408, (Slice(u8)){0});
}

function RpcHandler
rpc_handler_new(u64 uid, RpcHandlerFunc *f, void *ctx) {
  return (RpcHandler){ .uid = uid, .f = f, .ctx = ctx };
}

function void
slice_grow(usize *len, usize *cap, Mem_Base *mb, void **items, usize item_size) {
  void *new_items = mem_reserve(mb, item_size * *cap * 2);
  mem_commit(mb, new_items, item_size * *cap * 2);
  memmove(new_items, *items, item_size * *len);
  if (*items != NULL) {
    mem_decommit(mb, *items, item_size * *cap);
    mem_release(mb, *items, item_size * *cap);
  }
  *items = new_items;
  *cap *= 2;
}
