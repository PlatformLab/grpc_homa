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

import io.grpc.*;
import io.grpc.internal.*;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.logging.Level;

public class HomaServerStream implements ServerStream {
    // Used for extracting the header values.
    static final byte[] pathKey = (new String(":path").getBytes(
            StandardCharsets.US_ASCII));
    static final byte[] authorityKey = (new String(":authority").getBytes(
            StandardCharsets.US_ASCII));

    // Shared server state.
    HomaServer server;

    // The request message.
    HomaIncoming msg;

    // Information about the method being invoked (the ":path" header).
    String path;

    // The ":authority" header from the incoming request.
    String authority;

    // Used to pass incoming messages to gRPC.
    ServerStreamListener listener;

    // Needed by statsTraceCtx method.
    StatsTraceContext statsTraceCtx;

    // Metadata to send to the client with the response.
    Metadata headers;

    // Used to assemble outgoing messages. Position will be at the next
    // byte where info should be appended to the message. The header is
    // filled in only just before transmission.
    ByteBuffer xmitBuf;

    // Header for the outgoing message that is currently being assembled
    // in xmitBuffer.
    HomaWire.Header xmitHeader;

    // Payload for the response message, or null if none.
    InputStream responsePayload;

    /**
     * Constructor for HomaServerStream.
     * @param msg
     *      Message containing an incoming request.
     * @param server
     *      HomaServer associated with this stream.
     * @param transportListener
     *      Used to notify gRPC of the stream (at the appropriate time).
     */
    HomaServerStream(HomaIncoming msg, HomaServer server,
                     ServerTransportListener transportListener) {
        this.server = server;
        this.msg = msg;
        if (msg.headers == null) {
            System.out.printf("No initial metadata in incoming request.\n");
            return;
        }

        // Extract the ":path" and ":authority" headers (can't use Metadata
        // methods for this because they reject key names with colons).
        // Strip the leading "/" from the path.
        for (int i = 0; i < msg.headers.length/2; i += 2) {
            if (Arrays.equals(pathKey, msg.headers[i])) {
                path = new String(msg.headers[i+1], 1,
                        msg.headers[i+1].length-1, StandardCharsets.US_ASCII);
            }
            if (Arrays.equals(authorityKey, msg.headers[i])) {
                authority = new String(msg.headers[i+1], 0,
                        msg.headers[i+1].length, StandardCharsets.US_ASCII);
            }
        }
        if (path == null) {
            System.out.printf("Initial metadata didn't contain path\n");
        }
        System.out.printf("Path for incoming request is %s\n", path);
        Metadata metadata = HomaMetadata.newMetadata(msg.headers.length/2,
                msg.headers);
        statsTraceCtx = StatsTraceContext.newServerContext(
                server.streamTracerFactories, path, metadata);
        xmitBuf = ByteBuffer.allocateDirect(10000);
        xmitBuf.order(ByteOrder.BIG_ENDIAN);
        xmitBuf.position(HomaWire.Header.length);
        xmitHeader = new HomaWire.Header(msg.streamId.sid, msg.header.sequence,
                (byte) 0);
        transportListener.streamCreated(this, path, metadata);
    }

    @Override
    public void writeHeaders(Metadata headers) {
        int pos = xmitBuf.position();
        System.out.printf("HomaServerStream.writeHeaders invoked, pos %d\n",
                pos);
        byte[][] headerBytes = InternalMetadata.serialize(headers);
        HomaWire.serializeMetadata(headerBytes, xmitBuf);
        xmitHeader.initMdBytes = xmitBuf.position() - pos;
        xmitHeader.flags |= HomaWire.Header.initMdPresent;
        System.out.printf("%d header keys and values\n", headerBytes.length/2);
        for (int i = 0; i < headerBytes.length; i += 2) {
            System.out.printf("Response headers: key %s, value %s\n",
                    new String(headerBytes[i], StandardCharsets.UTF_8),
                    new String(headerBytes[i+1], StandardCharsets.UTF_8));
        }
    }

    @Override
    public void close(Status status, Metadata trailers) {
        System.out.printf("HomaServerStream.close invoked\n");

        // Add trailing metadata to the message.
        int pos = xmitBuf.position();
        byte[][] statusTrailer = new byte[2][];
        statusTrailer[0] = "grpc-status".getBytes(StandardCharsets.US_ASCII);
        statusTrailer[1] = (String.valueOf(status.getCode().value())).getBytes(
                StandardCharsets.US_ASCII);
        HomaWire.serializeMetadata(statusTrailer, xmitBuf);
        byte[][] trailerBytes = InternalMetadata.serialize(trailers);
        HomaWire.serializeMetadata(trailerBytes, xmitBuf);
        xmitHeader.trailMdBytes = xmitBuf.position() - pos;
        xmitHeader.flags |= HomaWire.Header.trailMdPresent;
        System.out.printf("%d trailer keys and values\n", trailerBytes.length/2);
        for (int i = 0; i < trailerBytes.length; i += 2) {
            System.out.printf("Response trailers: key %s, value %s\n",
                    new String(trailerBytes[i], StandardCharsets.UTF_8),
                    new String(trailerBytes[i+1], StandardCharsets.UTF_8));
        }

        // Add the payload to the message.
        if (responsePayload != null) {
            try {
                pos = xmitBuf.position();
                while (true) {
                    int data = responsePayload.read();
                    if (data == -1) {
                        break;
                    }
                    xmitBuf.put((byte) data);
                }
                int length = xmitBuf.position() - pos;
                xmitHeader.messageBytes = length;
                xmitHeader.flags |= HomaWire.Header.messageComplete;
                System.out.printf("HomaServerStream.writeMessage invoked (%d bytes)\n",
                        length);
            } catch (IOException e) {
            }
        }

        // Store the header in the output buffer.
        pos = xmitBuf.position();
        xmitBuf.position(0);
        xmitHeader.serialize(xmitBuf);
        xmitBuf.position(pos);

        // Send the message.
        System.out.printf("HomaServerStream sending response with header " +
                "length %d, initMdBytes %d, messageBytes %d, trailMdBytes %d, " +
                "total length %d\n", xmitHeader.length, xmitHeader.initMdBytes,
                xmitHeader.messageBytes, xmitHeader.trailMdBytes,
                xmitBuf.position());
        int err = server.homa.reply(msg.peer, xmitBuf);
        if (err < 0) {
            server.logger.log(Level.SEVERE, String.format(
                    "HomaClient.flush couldn't send request: %s\n",
                    server.homa.strerror(-err)));
        } else {
            System.out.printf("Sent reply to %s, id %d, %d bytes, " +
                            "header length %d\n",
                    msg.streamId.address.toString(), msg.homaId, pos,
                    xmitHeader.length);
        }
    }

    @Override
    public void cancel(Status status) {
        System.out.printf("HomaServerStream.cancel invoked\n");
    }

    @Override
    public void setDecompressor(Decompressor decompressor) {
        System.out.printf("HomaServerStream.setDecompressor invoked\n");
    }

    @Override
    public Attributes getAttributes() {
        System.out.printf("HomaServerStream.getAttributes invoked\n");
        return null;
    }

    @Override
    public String getAuthority() {
        System.out.printf("HomaServerStream.getAuthority returning %s\n",
                authority);
        return authority;
    }

    @Override
    public void setListener(ServerStreamListener serverStreamListener) {
        System.out.printf("HomaServerStream.setListener invoked\n");
        listener = serverStreamListener;
    }

    @Override
    public StatsTraceContext statsTraceContext() {
        System.out.printf("HomaServerStream.statsTraceContext invoked\n");
        return statsTraceCtx;
    }

    @Override
    public int streamId() {
        System.out.printf("HomaServerStream.statsTraceContext invoked\n");
        return 0;
    }

    @Override
    public void request(int numMessages) {
        System.out.printf("HomaServerStream.request invoked, numMessages %d\n",
                numMessages);
        if (listener != null) {
            listener.messagesAvailable(this.new HomaMessageProducer());
            listener.halfClosed();
        }
    }

    @Override
    public void writeMessage(InputStream message) {
        System.out.printf("HomaServerStream.writeMessage invoked\n");
        responsePayload = message;
    }

    @Override
    public void flush() {
        System.out.printf("HomaServerStream.flush invoked\n");
    }

    @Override
    public boolean isReady() {
        System.out.printf("HomaServerStream.isReady invoked\n");
        return false;
    }

    @Override
    public void optimizeForDirectExecutor() {
        System.out.printf("HomaServerStream.optimizeForDirectExecutor invoked\n");
    }

    @Override
    public void setCompressor(Compressor compressor) {
        System.out.printf("HomaServerStream.setCompressor invoked with %s\n",
                compressor.toString());
    }

    @Override
    public void setMessageCompression(boolean enable) {
        System.out.printf("HomaServerStream.setMessageCompression(%s) invoked\n",
                enable);
    }

    // This class is used to pass incoming message data to gRPC.
    protected class HomaMessageProducer
            implements StreamListener.MessageProducer {
        boolean done = false;
        @Override
        public InputStream next() {
            if (done) {
                System.out.printf("HomaMessageProducer.next returning false\n");
                return null;
            }
            System.out.printf("HomaMessageProducer.next returning message\n");
            done = true;
            return msg.message;
        }
    }
}
