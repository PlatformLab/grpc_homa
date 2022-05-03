/* Copyright (c) 2022 Stanford University
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

import io.grpc.*;
import io.grpc.internal.InternalServer;
import io.grpc.internal.ServerListener;
import io.grpc.internal.ServerTransport;
import io.grpc.internal.ServerTransportListener;
import io.grpc.InternalChannelz.SocketStats;

import java.io.IOException;
import java.net.SocketAddress;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ScheduledExecutorService;
import java.util.logging.Level;
import java.util.logging.Logger;

import com.google.common.util.concurrent.ListenableFuture;

/**
 * An instance of this class represents a server that will accept gRPC
 * requests arriving via Homa RPCs on a specific Homa port.
 */
public class HomaServer implements InternalServer, InternalWithLogId {
    // Port on which this server receives incoming requests.
    int port;

    // Homa socket corresponding to port.
    HomaSocket homa;

    // Used for logging interesting events.
    Logger logger = Logger.getLogger(InternalServer.class.getName());

    // Receives requests.
    ServerThread receiver;

    // Used to report certain events up to gRPC.
    ServerListener listener;

    // Only used to fetch transportListener.
    HomaTransport transport;

    // Used to report certain events up to gRPC.
    ServerTransportListener transportListener;

    // Needed by HomaServerStream.statsTraceContext
    List<? extends ServerStreamTracer.Factory> streamTracerFactories;

    /**
     * Construct a Homa server.
     * @param port
     *     Homa port number on which to listen for incoming requests.
     */
    HomaServer(int port, List<? extends ServerStreamTracer.Factory>
            streamTracerFactories) {
        this.port = port;
        this.homa = null;
        logger = Logger.getLogger(HomaServer.class.getName());
        receiver = new ServerThread(this);
        listener = null;
        transport = null;
        transportListener = null;
        this.streamTracerFactories = streamTracerFactories;
    }

    @Override
    public InternalLogId getLogId() {
        System.out.printf("HomaServer.getLogId invoked\n");
        return null;
    }

    @Override
    public void start(ServerListener listener) throws IOException {
        homa = new HomaSocket(port);
        receiver.start();
        System.out.printf("HomaServer.start opened Homa socket on port %d " +
                "and started receiver thread\n", port);
        this.listener = listener;
        transport = new HomaTransport();
        transportListener = listener.transportCreated(transport);
        transportListener.transportReady(Attributes.newBuilder().build());
    }

    @Override
    public void shutdown() {
        System.out.printf("HomaServer.shutdown invoked\n");
    }

    @Override
    public SocketAddress getListenSocketAddress() {
        System.out.printf("HomaServer.getListenSocketAddress invoked\n");
        return null;
    }

    @Override
    public InternalInstrumented<InternalChannelz.SocketStats> getListenSocketStats() {
        System.out.printf("HomaServer.getListenSocketStats invoked\n");
        return null;
    }

    @Override
    public List<? extends SocketAddress> getListenSocketAddresses() {
        System.out.printf("HomaServer.getListenSocketAddresses invoked\n");
        return new ArrayList<SocketAddress>();
    }

    @Override
    public List<InternalInstrumented<InternalChannelz.SocketStats>> getListenSocketStatsList() {
        System.out.printf("HomaServer.getListenSocketStatsList invoked\n");
        return null;
    }

    /**
     * This class represents a thread that receives incoming Homa requests
     * and handles them appropriately.
     */
    static class ServerThread extends Thread {
        // Shared client state.
        HomaServer server;

        ServerThread(HomaServer server) {
            this.server = server;
        }

        public void run() {
            try {
                while (true) {
                    HomaIncoming msg = new HomaIncoming();
                    String err = msg.read(server.homa,
                            HomaSocket.flagReceiveRequest);
                    if (err != null) {
                        server.logger.log(Level.SEVERE, err);
                        continue;
                    }
                    System.out.printf("Received request with Homa id %d from %s, " +
                                    "totalBytes %d, initMdBytes %d, messageBytes %d, " +
                                    "trailMdBytes %d, flags 0x%x\n",
                            msg.homaId, msg.peer.getInetSocketAddress(),
                            msg.length, msg.header.initMdBytes,
                            msg.header.messageBytes, msg.header.trailMdBytes,
                            msg.header.flags);
                    HomaServerStream stream = new HomaServerStream(msg, server,
                            server.transportListener);

                }
            } catch (Exception e) {
                String message = e.getMessage();
                if (message == null) {
                    message = "no information about cause";
                }
                server.logger.log(Level.SEVERE,
                        String.format("HomaServer.ServerThread crashed with " +
                                "%s exception: %s", e.getClass().getName(),
                                message));
            }
        }
    }

    /**
     * In the HTTP implementation of gRPC, there is one instance of this class
     * for each incoming connection. Since Homa is connectionless, a single
     * instance of this class is used for all incoming requests; the only
     * reason this class exists at all is because it's needed to retrieve a
     * ServerTransportListener.
     */
    static class HomaTransport implements ServerTransport {
        InternalLogId logId;

        HomaTransport() {
            logId = InternalLogId.allocate(getClass(), "<dummy>");
        }

        @Override
        public ListenableFuture<SocketStats> getStats() {
            System.out.printf("HomaTransport.getStats invoked\n");
            return null;
        }

        @Override
        public InternalLogId getLogId() {
            System.out.printf("HomaTransport.getLogId invoked\n");
            return logId;
        }

        @Override
        public void shutdown() {
            System.out.printf("HomaTransport.shutdown invoked\n");
        }

        @Override
        public void shutdownNow(Status reason) {
            System.out.printf("HomaTransport.shutdownNow invoked\n");
        }

        @Override
        public ScheduledExecutorService getScheduledExecutorService() {
            System.out.printf("HomaTransport.getScheduledExecutorService invoked\n");
            return null;
        }
    }
}
