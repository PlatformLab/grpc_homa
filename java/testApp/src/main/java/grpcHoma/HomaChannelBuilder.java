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

import io.grpc.ChannelCredentials;
import io.grpc.ChannelLogger;
import io.grpc.ManagedChannelBuilder;
import io.grpc.internal.*;

import java.net.SocketAddress;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/**
 * This class just contains boilerplate used to create Homa channels. It is
 * based heavily on InProcessChannelBuilder and NettyChannelBuilder from gRPC;
 * it's not documented very well because the whole mechanism doesn't really
 * make much sense to me :-(.
 */
public final class HomaChannelBuilder
        extends AbstractManagedChannelImplBuilder<HomaChannelBuilder> {

    private final ManagedChannelImplBuilder managedChannelImplBuilder;

    /**
     * Constructor for HomaChannelBuilders.
     * @param target
     *      Server associated with this channel, in the form host:port.
     */
    public HomaChannelBuilder(String target) {
        managedChannelImplBuilder = new ManagedChannelImplBuilder(target,
                new HomaChannelTransportFactoryBuilder(),
                null);
    }

    /**
     * Creates a new channel builder for a given target.
     * @param target
     *      Identifies the server associated with this builder (host:port).
     */
    public static HomaChannelBuilder forTarget(String target) {
        return new HomaChannelBuilder(target);
    }

    @Override
    protected ManagedChannelBuilder<?> delegate() {
        return managedChannelImplBuilder;
    }

    ClientTransportFactory buildTransportFactory() {
        return new HomaChannelBuilder.HomaClientTransportFactory();
    }

    private final class HomaChannelTransportFactoryBuilder
            implements ManagedChannelImplBuilder.ClientTransportFactoryBuilder {
        @Override
        public ClientTransportFactory buildClientTransportFactory() {
            return buildTransportFactory();
        }
    }

    /**
     * Creates Homa transports.
     */
    static final class HomaClientTransportFactory implements ClientTransportFactory {
        // True means this factory can no longer be used to create channels.
        private boolean closed;

        private ScheduledExecutorService schedExecService;

        public HomaClientTransportFactory() {
            closed = false;
            schedExecService = SharedResourceHolder.get(GrpcUtil.TIMER_SERVICE);
        }

        @Override
        public ConnectionClientTransport newClientTransport(SocketAddress addr,
                ClientTransportOptions options, ChannelLogger channelLogger) {
            if (closed) {
                throw new IllegalStateException("The Homa transport factory is closed.");
            }
            // TODO(carl-mastrangelo): Pass channelLogger in.
            return new HomaClientTransport(addr, options, channelLogger);
        }

        @Override
        public ScheduledExecutorService getScheduledExecutorService() {
            return schedExecService;
        }

        @Override
        public SwapChannelCredentialsResult swapChannelCredentials(ChannelCredentials channelCreds) {
            return null;
        }

        @Override
        public void close() {
            closed = true;
            SharedResourceHolder.release(GrpcUtil.TIMER_SERVICE, schedExecService);
            schedExecService = null;
        }
    }
}