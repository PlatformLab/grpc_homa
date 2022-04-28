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

import java.io.IOException;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.logging.Logger;

import io.grpc.ChannelLogger;
import io.grpc.Metadata;

/**
 * Each instance of this class stores information about one incoming Homa
 * message (either a request or a response). A gRPC message may require
 * more than one Homa message.
 */
class HomaIncoming {
    // The first part of the Homa message. This will be large enough to hold
    // most messages in their entirety; at minimum, it is guaranteed to be large
    // enough to hold the entire HomaWire::Header.
    static final int initialPayloadSize = 10000;
    ByteBuffer initialPayload;

    // Homa's identifier for the incoming message (or a negative errno if
    // there was an error receiving the message; in this case, no other
    // fields are valid except peer).
    long homaId;

    // Total number of bytes in the Homa message.
    int length;

    // Total number of bytes stored at initialPayload.
    int baseLength;

    // Header info extracted from initialPayload.
    HomaWire.Header header;

    // Information about the peer from which the message was received, plus
    // the Homa RPC's id.
    HomaSocket.RpcSpec peer;

    // Unique identifier for this stream.
    StreamId streamId;

    // Raw initial metadata extracted from the message (alternating keys and
    // values); null means there were none, or they have already been passed
    // to gRPC.
    byte[][] headers;

    // Raw trailing metadata extracted from the message (alternating keys and
    // values); null means there were none, or they have already been passed
    // to gRPC.
    byte[][] trailers;

    // The message payload from the message, or null if none.
    MessageStream message;

    HomaIncoming() {
        initialPayload = ByteBuffer.allocateDirect(initialPayloadSize);
        initialPayload.order(ByteOrder.BIG_ENDIAN);
        length = 0;
        baseLength = 0;
        header = new HomaWire.Header();
        peer = new HomaSocket.RpcSpec();
        headers = null;
        trailers = null;
        message = null;
    }

    /**
     * Read the first part of an incoming Homa request or response message,
     * and populate the structure with information for that message.
     * @param homa
     *      Used to receive an incoming message.
     * @param flags
     *      The flags value to pass to HomaSocket.receive.
     * @return
     *      Null (for success) or a string containing an error message (for
     *      logging) if there was a problem.
     */
    String read(HomaSocket homa, int flags) {
        peer.reset();
        length = homa.receive(initialPayload, flags, peer);
        homaId = peer.getId();
        if (length < 0) {
            return String.format("Error receiving Homa id %d from %s: %s",
                    homaId, peer.getInetSocketAddress().toString(),
                    HomaSocket.strerror(-length));
        }
        header.deserialize(initialPayload);
        streamId = new StreamId(peer.getInetSocketAddress(), header.sid);
        if ((header.flags & HomaWire.Header.initMdPresent) != 0) {
            headers = HomaWire.deserializeMetadata(initialPayload,
                    header.initMdBytes);
        }
        if ((header.flags & HomaWire.Header.trailMdPresent) != 0) {
            trailers = HomaWire.deserializeMetadata(initialPayload,
                    header.trailMdBytes);
        }
        if ((header.flags & HomaWire.Header.messageComplete) != 0) {
            int remaining = initialPayload.limit() - initialPayload.position();
            if (header.messageBytes == remaining) {
                message = new MessageStream(this);
            } else {
                System.out.printf("Incoming with initMdBytes %d, messageBytes " +
                        "%d, trailMdBytes %d, total length %d\n",
                        header.initMdBytes, header.trailMdBytes,
                        header.messageBytes, length);
                return String.format("Expected %d message bytes in Homa " +
                        "message from %s, got %d bytes (discarding data)",
                        remaining, streamId.address.toString(),
                        header.messageBytes);
            }
        }
        return null;
    }

    /**
     * An instance of this class represents an incoming gRPC message; it is
     * passed up to gRPC and used by gRPC to read the contents of the
     * message.
     */
    static private class MessageStream extends InputStream {

        // Information about the incoming message.
        HomaIncoming incoming;

        /**
         * Constructor for MessageStream.
         * @param incoming
         *      The actual message data is stored here.
         */
        public MessageStream(HomaIncoming incoming) {
            this.incoming = incoming;
        }

        @Override
        public int available() throws IOException {
            return incoming.initialPayload.limit()
                    - incoming.initialPayload.position();
        }

        @Override
        public int read() throws IOException {
            if (incoming.initialPayload.position()
                    >= incoming.initialPayload.limit()) {
                return -1;
            }
            return incoming.initialPayload.get();
        }
    }
}