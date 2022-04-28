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

import io.grpc.ServerBuilder;
import io.grpc.ServerStreamTracer;
import io.grpc.internal.AbstractServerImplBuilder;
import io.grpc.internal.InternalServer;
import io.grpc.internal.ServerImplBuilder;

import java.util.List;
import java.util.concurrent.TimeUnit;

/**
 * This class contains boilerplate used to create Homa servers. It is
 * based heavily on NettyServerBuilder from gRPC; it's not documented very
 * well because I'm not sure I understand the mechanism well enough to
 * document it :-(.
 */
public class HomaServerBuilder
        extends AbstractServerImplBuilder<HomaServerBuilder> {
    ServerImplBuilder serverImplBuilder;
    int port;

    /**
     * Constructor for HomaServerBuilder.
     *
     * @param port Homa port number on which to listen for incoming requests.
     */
    public HomaServerBuilder(int port) {
        this.port = port;
        serverImplBuilder = new ServerImplBuilder(
                new HomaClientTransportServersBuilder());

        // Without this, gRPC will attempt to use the result of
        // HomaTransport.getScheduledExecutorService to schedule a handshake
        // timeout (which is irrelevant for Homa).
        serverImplBuilder.handshakeTimeout(Long.MAX_VALUE,
                TimeUnit.MILLISECONDS);
    }

    /**
     * Create a new HomaServerBuilder for a given port.
     * @param port
     *      The Homa port on which the resulting server should listen for
     *      incoming Homa RPCs.
     * @return
     *      The new builder.
     */
    public static HomaServerBuilder forPort(int port) {
        return new HomaServerBuilder(port);
    }

    @Override
    protected ServerBuilder<?> delegate() {
        System.out.printf("HomaServerBuilder.delegate invoked\n");
        return serverImplBuilder;
    }

    private class HomaClientTransportServersBuilder
            implements ServerImplBuilder.ClientTransportServersBuilder {
        @Override
        public InternalServer buildClientTransportServers(
                List<? extends ServerStreamTracer.Factory> streamTracerFactories) {
            return new HomaServer(port, streamTracerFactories);
        }
    }
}

