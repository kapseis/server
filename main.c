#include "base.h"
#include "rpc.h"

#include "base.c"
#include "rpc.c"

#include <stdio.h>

s32
main(s32 argc, char *argv[]) {
#if OsHasFlags(OsFlags_Posix)
  signal(SIGPIPE, SIG_IGN);
#endif

  puts("Starting RPC server");

  RpcServer server;
  int s = init_rpc_server(&server);
  if (s != 0)
    return s;
  return run_rpc_server(&server);
}
