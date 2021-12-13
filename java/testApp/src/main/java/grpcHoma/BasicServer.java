package basic;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import io.grpc.Server;
import io.grpc.ServerBuilder;
import io.grpc.stub.StreamObserver;

/**
 * An object of this class represents a server that listens on a
 * given port and handles requests with the interface specified
 * by basic.proto.
 */
public class BasicServer {

    /**
     * Construct a BasicServer.
     * @param port
     *      Port number that the service should listen on.
     */
    public BasicServer(int port) throws IOException {
        server = ServerBuilder.forPort(port)
                .addService(new BasicService())
                .build();
        server.start();
    }
    
    /**
     * Shut down the server, so that it no longer responds to requests.
     */
    void close() {
        server.shutdownNow();
        try {
            server.awaitTermination(5, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            // Ignore exception and return.
        }
    }

    protected static class BasicService extends BasicGrpc.BasicImplBase {
        @Override
        public void ping(BasicProto.Request request,
                StreamObserver<BasicProto.Response> responseObserver) {
            if (request.getDataCount() != request.getRequestItems()) {
                System.out.printf("Expected %d items in Ping request, got %d\n",
                        request.getRequestItems(), request.getDataCount());
            }
            BasicProto.Response.Builder builder =
                    BasicProto.Response.newBuilder();
            int length = request.getReplyItems();
            for (int i = 0; i < length; i++) {
                    builder.addData(i);
            }
            responseObserver.onNext(builder.build());
            responseObserver.onCompleted();
        }
    }

    // gRPC server associated with this object.
    Server server;
}