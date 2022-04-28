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
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

import io.grpc.*;
import io.grpc.Deadline;
import io.grpc.internal.ClientStream;
import io.grpc.internal.ClientStreamListener;
import io.grpc.internal.GrpcUtil;
import io.grpc.internal.InsightBuilder;

/**
 * An instance of this class represents the client state for one active RPC.
 */
public class HomaClientStream implements ClientStream,
        ClientStreamListener.MessageProducer {
    // Transport this stream belongs to.
    HomaClientTransport transport;

    // Unique identifier for this stream.
    StreamId streamId;

    // Sequence number (within the stream) for next outgoing message.
    int nextXmitSequence;

    // Metadata to send to the server for this request.
    Metadata headers;

    // Used to assemble outgoing messages. Position will be at the next
    // byte where info should be appended to the message. The header is
    // filled in only just before transmission.
    ByteBuffer xmitBuf;

    // Header for the outgoing message that is currently being assembled
    // in xmitBuffer.
    HomaWire.Header xmitHeader;

    // Value for the ":path" value for outgoing initial metadata.
    String path;

    // Used to pass information from incoming Homa message up into gRPC.
    ClientStreamListener listener;

    // Current incoming message, or null if none.
    HomaIncoming incoming;

    /**
     * Construct a new HomaClientStream
     * @param transport
     *      State related to server for this RPC.
     * @param method
     *      Identifies the method that will be invoked on the server.
     * @param headers
     *      Headers to include in outgoing message.
     * @param callOptions
     *      Various options.
     * @param tracers
     *      ??
     */
    HomaClientStream(HomaClientTransport transport,
                     MethodDescriptor<?, ?> method, Metadata headers,
                     CallOptions callOptions, ClientStreamTracer[] tracers) {
        HomaClient client = transport.client;
        this.transport = transport;
        synchronized(client) {
            streamId = new StreamId(transport.serverAddress, client.nextSid);
            transport.client.nextSid++;
            transport.client.streams.put(streamId, this);
        }
        nextXmitSequence = 1;
        this.headers = headers;
        xmitBuf = ByteBuffer.allocateDirect(10000);
        xmitBuf.order(ByteOrder.BIG_ENDIAN);
        xmitBuf.position(HomaWire.Header.length);
        xmitHeader = new HomaWire.Header(streamId.sid, nextXmitSequence,
                HomaWire.Header.isRequest);
        path = "/" + method.getFullMethodName();
        listener = null;
        incoming = null;

        System.out.printf("HomaClientStream constructor invoked, " +
                "callOptions %s, sequence %d\n", callOptions.toString(),
                streamId.sid);
        int pos = xmitBuf.position();
        byte[][] autoHeaders = new byte[4][];
        autoHeaders[0] = ":path".getBytes(StandardCharsets.US_ASCII);
        autoHeaders[1] = ("/" + method.getFullMethodName()).getBytes(
                StandardCharsets.US_ASCII);
        autoHeaders[2] = ":authority".getBytes(StandardCharsets.US_ASCII);
        autoHeaders[3] = transport.options.getAuthority().getBytes(
                StandardCharsets.US_ASCII);
        HomaWire.serializeMetadata(autoHeaders, xmitBuf);
        byte[][] headerBytes = InternalMetadata.serialize(headers);
        HomaWire.serializeMetadata(headerBytes, xmitBuf);
        xmitHeader.initMdBytes = xmitBuf.position() - pos;
        xmitHeader.flags |= HomaWire.Header.initMdPresent;

        System.out.printf("%d header keys and values\n", headerBytes.length/2);
        for (int i = 0; i < headerBytes.length; i += 2) {
            System.out.printf("Initial metadata key %s, value %s\n",
                    new String(headerBytes[i], StandardCharsets.UTF_8),
                    new String(headerBytes[i+1], StandardCharsets.UTF_8));
        }
    }

    @Override
    public void cancel(Status reason) {
        System.out.printf("HomaClientStream.cancel invoked with reason %s\n",
                reason.toString());
    }

    @Override
    public void halfClose() {
        System.out.printf("HomaClientStream.halfClose invoked\n");
        flush();
    }

    @Override
    public void setAuthority(String authority) {
        System.out.printf("HomaClientStream.setAuthority invoked\n");
    }

    @Override
    public void setFullStreamDecompression(boolean fullStreamDecompression) {
        System.out.printf("HomaClientStream.setFullStreamDecompression invoked\n");
    }

    @Override
    public void setDecompressorRegistry(DecompressorRegistry decompressorRegistry) {
        System.out.printf("HomaClientStream.setDecompressorRegistry invoked\n");
    }

    @Override
    public void start(ClientStreamListener listener) {
        System.out.printf("HomaClientStream.start invoked\n");
        this.listener = listener;
    }

    @Override
    public void setMaxInboundMessageSize(int maxSize) {
        System.out.printf("HomaClientStream.setMaxInboundMessageSize invoked\n");
    }

    @Override
    public void setMaxOutboundMessageSize(int maxSize) {
        System.out.printf("HomaClientStream.setMaxOutboundMessageSize invoked\n");
    }

    @Override
    public void setDeadline(Deadline deadline) {
        System.out.printf("HomaClientStream.setDeadline invoked\n");
    }

    @Override
    public Attributes getAttributes() {
        System.out.printf("HomaClientStream.getAttributes invoked\n");
        return null;
    }

    @Override
    public void appendTimeoutInsight(InsightBuilder insight) {
        System.out.printf("HomaClientStream.appendTimeoutInsight invoked\n");
    }

    @Override
    public void request(int numMessages) {
        System.out.printf("HomaClientStream.request invoked, " +
                "numMessages %d\n", numMessages);
    }

    @Override
    public void writeMessage(InputStream message) {
        try {
            int pos = xmitBuf.position();
            while (true) {
                int data = message.read();
                if (data == -1) {
                    break;
                }
                xmitBuf.put((byte) data);
            }
            int length = xmitBuf.position() - pos;
            xmitHeader.messageBytes = length;
            xmitHeader.flags |= HomaWire.Header.messageComplete;
            System.out.printf("HomaClientStream.writeMessage invoked (%d bytes)\n",
                    length);
        }
        catch (IOException e) {
        }
    }

    @Override
    public void flush() {
        System.out.printf("HomaClientStream.flush invoked\n");
        if ((xmitBuf.position() <= xmitHeader.length)
                && (xmitHeader.flags == 0)) {
            return;
        }

        // Store the header in the output buffer.
        int pos = xmitBuf.position();
        xmitBuf.position(0);
        xmitHeader.serialize(xmitBuf);
        xmitBuf.position(pos);

        HomaSocket.RpcSpec spec = new HomaSocket.RpcSpec(
                transport.serverAddress);
        long id = transport.client.homa.send(spec, xmitBuf);
        if (id < 0) {
            transport.client.logger.log(ChannelLogger.ChannelLogLevel.ERROR,
                    String.format("HomaClient.flush couldn't send request: %s\n",
                    transport.client.homa.strerror((int) -id)));
        } else {
            System.out.printf("Sent request to %s, id %d, %d bytes, " +
                    "header length %d\n",
                    transport.serverAddress.toString(), id, pos,
                    xmitHeader.length);
        }
    }

    @Override
    public boolean isReady() {
        System.out.printf("HomaClientStream.isReady invoked\n");
        return true;
    }

    @Override
    public void optimizeForDirectExecutor() {
        System.out.printf("HomaClientStream.optimizeForDirectExecutor invoked\n");
    }

    @Override
    public void setCompressor(Compressor compressor) {
        System.out.printf("HomaClientStream.setCompressor invoked\n");
    }

    @Override
    public void setMessageCompression(boolean enable) {
        System.out.printf("HomaClientStream.setMessageCompression invoked\n");
    }

    /**
     * Extract the response status from trailing metadata.
     * @param trailers
     *      Metadata that is expected to contain status information.
     */
    private Status statusFromTrailers(Metadata trailers) {
        Status status = trailers.get(InternalStatus.CODE_KEY);
        if (status != null) {
            return status.withDescription(trailers.get(InternalStatus.MESSAGE_KEY));
        }
        return Status.UNKNOWN.withDescription("missing GRPC status in response");
    }

    /**
     * This method is responsible for transferring information about
     * incoming messages to gRPC at appropriate times. It is invoked in
     * places where it may have become possible to transfer additional data.
     */
    void transferData() {
        if (listener == null) {
            return;
        }
        if (incoming.headers != null) {
            listener.headersRead(HomaMetadata.newMetadata(
                    incoming.headers.length/2, incoming.headers));
            incoming.headers = null;
        }
        if (incoming.message != null) {
            listener.messagesAvailable(this);
        }
        if ((incoming.headers == null) && (incoming.message == null)
                && (incoming.trailers != null)) {
            Metadata trailers = HomaMetadata.newMetadata(
                    incoming.trailers.length/2, incoming.trailers);
            listener.closed(statusFromTrailers(trailers),
                    ClientStreamListener.RpcProgress.PROCESSED, trailers);
            synchronized (transport.client) {
                transport.client.streams.remove(streamId);
            }
        }

    }
    /**
     * Do whatever is needed to handle a Homa message that has just arrived for
     * this stream.
     * @param incoming
     *      Information about the incoming message.
     */
    void handleIncoming(HomaIncoming incoming) {
        this.incoming = incoming;
        transferData();
    }

    @Override
    public InputStream next() {
        InputStream result = null;
        if (incoming != null) {
            result = incoming.message;
            incoming.message = null;
            transferData();
        }
        return result;
    }
}