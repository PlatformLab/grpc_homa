GTEST_INCLUDE_PATH = ../googletest/googletest/include
GTEST_LIB_PATH = ../googletest/build/lib
PROTOC = protoc
CXX = g++
INCLUDES = -I ../install/include \
           -I /users/ouster/homaModule \
           -I ../grpc \
           -I ../grpc/third_party/abseil-cpp \
           -I $(GTEST_INCLUDE_PATH)
CXXFLAGS += -g -std=c++11 -Wall -Werror -fno-strict-aliasing $(INCLUDES) -MD
CFLAGS = -Wall -Werror -fno-strict-aliasing -g -MD

OBJS =      homa_client.o \
	    homa_incoming.o \
	    homa_listener.o \
	    homa_stream.o \
	    stream_id.o \
	    util.o \
	    wire.o

HOMA_OBJS = homa_api.o

TEST_OBJS = mock.o \
            test_incoming.o \
            test_listener.o \
            test_stream.o

LDFLAGS += -L/usr/local/lib `pkg-config --libs protobuf grpc++`\
           -pthread\
           -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed\
           -ldl

GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`
PROTOS_PATH = .
PKG_CONFIG_PATH = /ouster/install/lib/pkgconfig
export PKG_CONFIG_PATH

all: test_client test_server tcp_test
	
test_client: test.grpc.pb.o test.pb.o test_client.o $(OBJS) $(HOMA_OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@
	
test_server: test.grpc.pb.o test.pb.o test_server.o $(OBJS) $(HOMA_OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@
	
tcp_test: test.grpc.pb.o test.pb.o tcp_test.o
	$(CXX) $^ $(LDFLAGS) -o $@
	
unit: $(OBJS) $(TEST_OBJS) $(GTEST_LIB_PATH)/libgtest_main.a \
	        $(GTEST_LIB_PATH)/libgtest.a
	$(CXX) $^ $(LDFLAGS) -o $@
	
test: unit
	./unit --gtest_brief=1
	
homa_api.o: /users/ouster/homaModule/homa_api.c
	cc -c $(CFLAGS) $< -o $@
	
clean:
	rm -f test_client test_server unit tcp_test *.o .deps
	
%.o: %.cc
	$(CXX) -c $(CXXFLAGS) -std=c++17 $< -o $@

%.grpc.pb.cc: %.proto %.pb.cc
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<

%.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<
	
.PHONY: test clean
	
# This magic (along with the -MD gcc option) automatically generates makefile
# dependencies for header files included from source files we compile,
# and keeps those dependencies up-to-date every time we recompile.
# See 'mergedep.pl' for more information.
.deps: $(wildcard *.d)
	@mkdir -p $(@D)
	perl mergedep.pl $@ $^
-include .deps