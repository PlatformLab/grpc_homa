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

    // If the message is longer than initialPayloadSize, holds all of the
    // bytes after those in initialPayload.
    ByteBuffer tail;

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
     * Read an incoming Homa request or response message and populate the
     * structure with information about that message. This method also
     * takes care of sending automatic responses for streaming RPCs and
     * discarding those responses.
     * @param homa
     *      Used to receive an incoming message.
     * @param flags
     *      The flags value to pass to HomaSocket.receive.
     * @return
     *      Null (for success) or a string containing an error message (for
     *      logging) if there was a problem.
     */
    String read(HomaSocket homa, int flags) {
        // This loop takes care of discarding automatic responses.
        while (true) {
            peer.reset();
            length = homa.receive(initialPayload,
                    flags | HomaSocket.flagReceivePartial, peer);
            homaId = peer.getId();
            if (length < 0) {
                return String.format("Error receiving Homa id %d from %s: %s",
                        homaId, peer.getInetSocketAddress().toString(),
                        HomaSocket.strerror(-length));
            }
            header.deserialize(initialPayload);
            if ((header.flags & HomaWire.Header.emptyResponse) == 0) {
                break;
            }
        }
        baseLength = initialPayload.limit();
        streamId = new StreamId(peer.getInetSocketAddress(), header.sid);
        int expected = HomaWire.Header.length + header.initMdBytes
                + header.messageBytes + header.trailMdBytes;
        if (length != expected) {
            return String.format("Bad message length %d (expected " +
                    "%d); initMdLength %d, messageLength %d, trailMdLength %d " +
                    "header length %d", length, expected, header.initMdBytes,
                    header.messageBytes, header.trailMdBytes,
                    HomaWire.Header.length);
        }

        // Read the tail of the message, if needed.
        if (length != baseLength) {
            tail = ByteBuffer.allocateDirect(length - baseLength);
            tail.order(ByteOrder.BIG_ENDIAN);
            int tailLength = homa.receive(initialPayload, flags, peer);
            if (tailLength < 0) {
                return String.format("Error while receiving tail of Homa id " +
                                "%d from %s: %s", homaId,
                        peer.getInetSocketAddress().toString(),
                        HomaSocket.strerror(-tailLength));
            }
            if (tailLength != (length - baseLength)) {
                return String.format("Tail of Homa message has wrong length: " +
                                "expected %d bytes, got %d bytes", length - baseLength,
                        tailLength);
            }
        }

        // Separate the three major sections of payload (headers, message,
        // trailers).
        if ((header.flags & HomaWire.Header.initMdPresent) != 0) {
            headers = HomaWire.deserializeMetadata(
                    getBuffer(header.initMdBytes), header.initMdBytes);
        }
        if ((header.flags & HomaWire.Header.trailMdPresent) != 0) {
            trailers = HomaWire.deserializeMetadata(
                    getBuffer(header.trailMdBytes), header.trailMdBytes);
        }
        if ((header.flags & HomaWire.Header.messageComplete) != 0) {
            message = new MessageStream(this);
        }
        return null;
    }

    /**
     * Returns a ByteBuffer that can be used to extract the next numBytes of
     * data from the message. It normally returns either initialPayload or
     * tail, but if the desired range crosses the boundary between these
     * two then it creates a new ByteBuffer by copying data from these two.
     * @param numBytes
     *      The caller wants this many bytes contiguous in a single
     *      ByteBuffer. The caller must ensure that at least this many
     *      bytes are available, between initialPayload and tail together.
     */
    ByteBuffer getBuffer(int numBytes) {
        int initSize = initialPayload.remaining();
        if (numBytes <= initSize) {
            return initialPayload;
        }
        if (initSize == 0) {
            return tail;
        }
        ByteBuffer spliced = ByteBuffer.allocate(numBytes);
        spliced.order(ByteOrder.BIG_ENDIAN);
        spliced.put(initialPayload);
        int oldLimit = tail.limit();
        tail.limit(numBytes - initSize);
        spliced.put(tail);
        tail.limit(oldLimit);
        spliced.flip();
        return spliced;
    }

    /**
     * An instance of this class represents an incoming gRPC message; it is
     * passed up to gRPC and used by gRPC to read the contents of the
     * message.
     */
    static private class MessageStream extends InputStream {

        // Information about the incoming message.
        HomaIncoming incoming;

        // Keeps track of whether the two parts of the payload have
        // already been read.
        boolean initialSent = false;
        boolean tailSent = false;

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