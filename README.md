This project adds Homa support to gRPC, so that gRPC applications can use
Homa instead of TCP for transport.

- This project is no longer under active development. At the time development
  was suspended (late 2023) C++ support was functional and Java support
  was partially implemented. I can't promise support for the existing
  code, but if you wish to continue development of this I may be able
  to provide some advice.

- The head is currently based on gRPC v. 1.57.0 (and there is a branch
  named grpc-1.57.0 that will track this version of gRPC). There are
  also branches grpc-1.43.0 and grpc-1.41.0, which represent the
  most recent code to work on those branches. Older branches are not
  actively maintained.

- Known limitations:
  - grpc_homa currently supports only insecure channels.

- Initial performance measurements show that short RPCs complete about
  40% faster with Homa than with TCP (about 55 us round trip for Homa,
  vs. 90 us for TCP).

### How to use grpc_homa
- You will need to download the
  [Linux kernel driver for Homa](https://github.com/PlatformLab/HomaModule).
  Compile it as described in that repo and install it on all the machines
  where you plan to use gRPC.

- Configure the Makefile as described in the comments at the top (sorry...
  I know this shouldn't need to be done manually).

- Type `make`. This will build `libhoma.a`, which you should link with
  your applications.

- When compiling your applications, use `-I` to specify this directory,
  then `#include homa_client.h` as needed for clients and
  `#include homa_listener.h` as needed for servers.

- On clients, pass `HomaClient::insecureChannelCredentials()` to
  `grpc::CreateChannel` instead of `grpc::InsecureChannelCredentials()`
  to create a channel that uses Homa.
  For an example of a simple but complete client, see `test_client.cc`.

- On servers, pass `HomaListener::insecureCredentials()` to
  `grpc::AddListeningPort` instead of `grpc::InsecureServerCredentials()`.
  For an example of a simple but complete server, see `test_server.cc`.

- Once you have done this, all your existing gRPC-based code should
  work just fine.
