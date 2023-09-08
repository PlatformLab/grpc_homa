This project adds Homa support to gRPC, so that gRPC applications can use
Homa instead of TCP for transport.

- This project is still in a relatively early stage of development, but C++
  support is functional as of November 2021. Next up is Java support.

- Please contact me if you have any problems; I'm happy to provide
  advice and support.

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

- On clients, invoke `HomaClient::createInsecureChannel` to create
  channels instead of `grpc::CreateChannel`. It takes a single argument,
  which is a string of the form host:port identifying the Homa server.
  For an example of a simple but complete client, see `test_client.cc`

- On servers, pass `HomaListener::insecureCredentials()` to
  `AddListeningPort` instead of `grpc::InsecureServerCredentials()`.
  For an example of a simple but complete server, see `test_server.cc`.

- Once you have done this, all your existing gRPC-based code should
  work just fine.
