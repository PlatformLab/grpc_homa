PROTOC = protoc
CXX = g++
INCLUDES = -I ../install/include \
           -I /users/ouster/homaModule \
           -I ../grpc \
           -I ../grpc/third_party/abseil-cpp
CXXFLAGS += -g -std=c++11 -Wall -Werror -fno-strict-aliasing $(INCLUDES)
CFLAGS = -Wall -Werror -fno-strict-aliasing -O3

LDFLAGS += -L/usr/local/lib `pkg-config --libs protobuf grpc++`\
           -pthread\
           -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed\
           -ldl

GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`
PROTOS_PATH = .
PKG_CONFIG_PATH = /ouster/install/lib/pkgconfig
export PKG_CONFIG_PATH

all: test_client test_server
	
test_client: test_client.o homa_api.o test.grpc.pb.o test.pb.o
	$(CXX) $^ $(LDFLAGS) -o $@
	
test_server: test_server.o homa_listener.o homa_api.o test.grpc.pb.o test.pb.o
	$(CXX) $^ $(LDFLAGS) -o $@
	
homa_api.o: /users/ouster/homaModule/homa_api.c
	cc -c $(CFLAGS) $< -o $@
	
clean:
	rm testClient testServer *.o
	
%.o: %.cc
	$(CXX) -c $(CXXFLAGS) -std=c++17 $< -o $@

%.grpc.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<

%.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<