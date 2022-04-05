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

    // Address corresponding to peer.
    InetSocketAddress peerAddress;

    // Header metadata extracted from the message; null if there were
    // none, or if they have already been passed to gRPC.
    Metadata headers;

    // Trailing metadata extracted from the message; null if there were
    // none, or if they have already been passed to gRPC.
    Metadata trailers;

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
     * @param logger
     *      Used to record information about errors.
     * @return
     *      Zero (for success) or a negative errno after a failure (in which
     *      case only the peer and id fields will be valid).
     */
    int read(HomaSocket homa, int flags, ChannelLogger logger) {
        peer.reset();
        length = homa.receive(initialPayload, flags, peer);
        homaId = peer.getId();
        peerAddress = peer.getInetSocketAddress();
        if (length < 0) {
            return length;
        }
        header.deserialize(initialPayload);
        if ((header.flags & HomaWire.Header.initMdPresent) != 0) {
            headers = HomaWire.deserializeMetadata(initialPayload,
                    header.initMdBytes);
        }
        if ((header.flags & HomaWire.Header.trailMdPresent) != 0) {
            trailers = HomaWire.deserializeMetadata(initialPayload,
                    header.trailMdBytes);
        }
        if ((header.flags & HomaWire.Header.messageComplete)!= 0) {
            int remaining = initialPayload.limit() - initialPayload.position();
            if (header.messageBytes == remaining) {
                message = new MessageStream(this);
            } else {
                logger.log(ChannelLogger.ChannelLogLevel.ERROR, String.format(
                        "Expected %d message bytes in incoming message from " +
                        "%s, got %d bytes (discarding data): %s",
                        peerAddress.toString(), remaining, header.messageBytes));
            }
        }
        return 0;
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