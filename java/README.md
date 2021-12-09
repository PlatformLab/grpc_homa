This directory contains Java support for Homa in gRPC. It has three
main subdirectories:
- homaJni: contains JNI native support for invoking Homa kernel calls.
- grpcHoma: a Java library that encapsulates Homa support. Its .jar
  file includes homaJni, so it is complete and self-contained.
- testApp: a main program that can be used to test and exercise grpcHoma.
  Invoke it with the --help option to find out more.
