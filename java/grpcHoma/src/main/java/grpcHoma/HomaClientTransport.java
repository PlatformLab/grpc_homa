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

import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.util.concurrent.Executor;
import java.util.concurrent.ScheduledExecutorService;

import io.grpc.Attributes;
import io.grpc.CallOptions;
import io.grpc.ChannelLogger;
import io.grpc.ClientStreamTracer;
import io.grpc.InternalLogId;
import io.grpc.Metadata;
import io.grpc.MethodDescriptor;
import io.grpc.Status;
import io.grpc.internal.ClientStream;
import io.grpc.internal.ClientTransportFactory.ClientTransportOptions;
import io.grpc.internal.ConnectionClientTransport;
import io.grpc.InternalChannelz.SocketStats;

import com.google.common.util.concurrent.ListenableFuture;

/**
 * An instance of this class is used to create streams that can communicate
 * with a specific gRPC server.
 */
public class HomaClientTransport implements ConnectionClientTransport {
    // The following are copies of construtor arguments.
    HomaClient client;
    InetSocketAddress serverAddress;
    ClientTransportOptions options;
    ChannelLogger logger;

    // Unique identifier used for logging.
    InternalLogId logId;

    // Used to notify gRPC of various interesting things happening on
    // this transport. Null means the transport hasn't been started yet.
    Listener listener;

    /**
     * Constructor for HomaClientTransports; normally invoked indirectly
     * though a HomaChannelBuilder.
     * @param client
     *      Shared client state.
     * @param serverAddress
     *      Location of the server for requests.
     * @param options
     *      Various parameters that may be used to configure the channel.
     * @param channelLogger
     *      Used for any log messages related to this transport and its streams.
     */
    HomaClientTransport(HomaClient client, SocketAddress serverAddress,
            ClientTransportOptions options,
            ChannelLogger channelLogger) {
        System.out.printf("Constructing HomaClientTransport for %s, authority %s\n",
                serverAddress.toString(), options.getAuthority());
        this.client = client;
        this.serverAddress = (InetSocketAddress) serverAddress;
        this.options = options;
        logId = InternalLogId.allocate(getClass(), serverAddress.toString());
        listener = null;
        logger = channelLogger;
    }

    @Override
    public ListenableFuture<SocketStats> getStats() {
        System.out.printf("HomaClientTransport.getStats invoked\n");
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
        logger.log(ChannelLogger.ChannelLogLevel.ERROR, String.format(
                "Creating HomaClientStream for %s", serverAddress.toString()));
        int id = client.nextId;
        client.nextId++;
        return new HomaClientStream(this, id, method, headers,
                callOptions, tracers);
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
        this.listener = listener;
        listener.transportReady();
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