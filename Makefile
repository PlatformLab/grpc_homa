DEBUG := no
ifeq ($(DEBUG),no)
    GRPC_INSTALL_DIR := /ouster/install.release
    DEBUG_FLAGS := -O3 -DNDEBUG
else
    GRPC_INSTALL_DIR := /ouster/install.debug
    DEBUG_FLAGS := -g
endif

GRPC_LIBS := $(shell export PKG_CONFIG_PATH=$(GRPC_INSTALL_DIR)/lib/pkgconfig; \
	pkg-config --libs protobuf grpc++)
ifeq ($(DEBUG),yes)
    GRPC_LIBS := $(subst -lprotobuf,-lprotobufd,$(GRPC_LIBS))
endif

GTEST_INCLUDE_PATH = ../googletest/googletest/include
GTEST_LIB_PATH = ../googletest/build/lib
export PKG_LIB_PATH = $(GRPC_INSTALL_DIR)/lib/pkgconfig
GRPC_DIR = ../grpc
PROTOC = $(GRPC_INSTALL_DIR)/bin/protoc
CXX = g++
INCLUDES = -I $(GRPC_INSTALL_DIR)/include \
           -I /users/ouster/homaModule \
           -I $(GRPC_DIR) \
           -I $(GRPC_DIR)/third_party/abseil-cpp \
           -I $(GTEST_INCLUDE_PATH)
CXXFLAGS += $(DEBUG_FLAGS) -std=c++17 -Wall -Werror -fno-strict-aliasing \
         $(INCLUDES) -MD
CFLAGS = -Wall -Werror -fno-strict-aliasing $(DEBUG_FLAGS) -MD

OBJS =      homa_client.o \
	    homa_incoming.o \
	    homa_listener.o \
	    homa_stream.o \
	    stream_id.o \
	    time_trace.o \
	    util.o \
	    wire.o

HOMA_OBJS = homa_api.o

TEST_OBJS = mock.o \
            test_incoming.o \
            test_listener.o \
            test_stream.o

LDFLAGS += -L/usr/local/lib $(GRPC_LIBS)\
           -pthread\
           -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed\
           -ldl

GRPC_CPP_PLUGIN = $(GRPC_INSTALL_DIR)/bin/grpc_cpp_plugin
PROTOS_PATH = .

all: stress test_client test_server tcp_test
	
stress: stress.grpc.pb.o stress.pb.o stress.o $(OBJS) $(HOMA_OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@

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
	rm -f test_client test_server unit tcp_test *.o *.pb.* .deps
	
install: all
	rsync -avz stress test_client test_server node-1:
	rsync -avz stress test_client test_server node-2:
	rsync -avz stress test_client test_server node-3:
	rsync -avz stress test_client test_server node-4:
	
%.o: %.cc
	$(CXX) -c $(CXXFLAGS) $< -o $@
	
%.o: %.c
	cc -c $(CFLAGS) $< -o $@

%.grpc.pb.cc %.grpc.pb.h: %.proto %.pb.cc
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

%.pb.cc %.pb.h: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<
	
.PHONY: test clean
.PRECIOUS: test.grpc.pb.h test.grpc.pb.cc test.pb.h test.pb.cc
	
test_client.o test_server.o tcp_test.o : test.grpc.pb.h test.pb.h

stress.o: stress.grpc.pb.h stress.pb.h

# This magic (along with the -MD gcc option) automatically generates makefile
# dependencies for header files included from source files we compile,
# and keeps those dependencies up-to-date every time we recompile.
# See 'mergedep.pl' for more information.
.deps: $(wildcard *.d)
	@mkdir -p $(@D)
	perl mergedep.pl $@ $^
-include .deps

# The following target is useful for debugging Makefiles; it
# prints the value of a make variable.
print-%:
	@echo $* = $($*)