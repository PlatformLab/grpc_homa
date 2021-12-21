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

package grpcHoma;

import java.net.SocketAddress;
import java.util.concurrent.Executor;
import java.util.concurrent.ScheduledExecutorService;

import io.grpc.*;
import io.grpc.internal.*;
import io.grpc.InternalChannelz.SocketStats;

import com.google.common.util.concurrent.ListenableFuture;

/**
 * An instance of this class is used to create streams that can communicate
 * with a given gRPC server.
 */
public class HomaClientTransport implements ConnectionClientTransport {

    // Unique identifier used for logging.
    InternalLogId logId;

    /**
     * Constructor for HomaClientTransports; normally invoked indirectly
     * though a HomaChannelBuilder.
     * @param serverAddress
     *      Location of the server for requests.
     * @param options
     *      Various parameters that may be used to configure the channel.
     * @param channelLogger
     *      Used for logging??
     */
    HomaClientTransport(SocketAddress serverAddress,
            ClientTransportFactory.ClientTransportOptions options,
            ChannelLogger channelLogger) {
        System.out.printf("Constructing HomaClientTransport\n");
        logId = InternalLogId.allocate(getClass(), "xyzzy");
    }

    @Override
    public ListenableFuture<SocketStats> getStats() {
        System.out.printf("HomaClientTransport.getStats invoked\n");
        return null;
    }

    /**
     * Returns a Homa-based channel that can be used to communicate with a
     * given server host.
     * @param hostName
     *      Name of the desired server host.
     * @param port
     *      Number of the port for the server application.
     */
    public static ManagedChannel newChannel(String hostName, int port) {
        return null;
    }

    @Override
    public InternalLogId getLogId() {
        return logId;
    }

    @Override
    public ClientStream newStream(MethodDescriptor<?, ?> method,
            Metadata headers, CallOptions callOptions,
            ClientStreamTracer[] tracers) {
        System.out.printf("HomaClientTransport.newStream invoked\n");
        return null;
    }

    @Override
    public void ping(PingCallback callback, Executor executor) {
        System.out.printf("HomaClientTransport.ping invoked\n");
    }

    @Override
    public Attributes getAttributes() {
        System.out.printf("HomaClientTransport.getAttributes invoked\n");
        return null;
    }

    @Override
    public Runnable start(Listener listener) {
        System.out.printf("HomaClientTransport.start invoked\n");
        return null;
    }

    @Override
    public void shutdown(Status reason) {
        System.out.printf("HomaClientTransport.shutdown invoked\n");
    }

    @Override
    public void shutdownNow(Status reason) {
        System.out.printf("HomaClientTransport.shutdownNow invoked\n");
    }
}