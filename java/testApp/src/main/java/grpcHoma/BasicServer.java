/* Copyright (c) 2021 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

package basic;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import grpcHoma.HomaServerBuilder;
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
     * @param useHoma
     *      True means that Homa should be used for transport; false means TCP.
     */
    public BasicServer(int port, boolean useHoma) throws IOException {
        if (useHoma) {
            server = HomaServerBuilder.forPort(port)
                    .addService(new BasicService())
                    .build();
        } else {
            server = ServerBuilder.forPort(port)
                    .addService(new BasicService())
                    .build();
        }
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
            System.out.printf("BasicService.ping invoked\n");
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