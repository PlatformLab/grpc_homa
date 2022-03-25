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

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

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

    // Metadata extracted from the message, or null if none.
    Metadata metadata;

    HomaIncoming() {
        initialPayload = ByteBuffer.allocateDirect(initialPayloadSize);
        initialPayload.order(ByteOrder.BIG_ENDIAN);
        length = 0;
        baseLength = 0;
        header = new HomaWire.Header();
        peer = new HomaSocket.RpcSpec();
    }

    /**
     * Read the first part of an incoming Homa request or response message,
     * and populate the structure with information for that message.
     * @param homa
     *      Used to receive an incoming message.
     * @param flags
     *      The flags value to pass to HomaSocket.receive.
     * @return
     *      Zero (for success) or a negative errno after a failure (in which
     *      case only the peer and id fields will be valid).
     */
    int read(HomaSocket homa, int flags) {
        peer.reset();
        length = homa.receive(initialPayload, flags, peer);
        homaId = peer.getId();
        if (length < 0) {
            return length;
        }
        header.deserialize(initialPayload);
        return 0;
    }
}