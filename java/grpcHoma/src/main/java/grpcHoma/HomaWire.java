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

import java.nio.ByteBuffer;
import java.util.ArrayList;

import io.grpc.Metadata;
import io.grpc.HomaMetadata;

/**
 * This class encapsulates information about how to serialize information into
 * Homa messages and deserialize information out of Homa messages. Note: this
 * class must be consistent with the definitions in the C++ class "wire.h" in
 * the C++ implementation of Homa support for gRPC, in order to maintain
 * interoperability between the C++ and Java implementations.
 */
class HomaWire {
    /**
     * Defines the fields of a message header.
     */
    static public class Header {
        // Unique identifier for this stream (all messages in the stream will
        // contain this value).
        int sid = 0;

        // Position of this Homa message among all of those sent on the
        // stream. Used on the other end to make sure that messages are
        // processed in order. The first number for each stream is 1.
        int sequence = 0;

        // Number of bytes of initial metadata (may be zero), which follows
        // the header in the Homa message.
        int initMdBytes = 0;

        // Number of bytes of trailing metadata (may be zero), which follows
        // the initial metadata.
        int trailMdBytes = 0;

        // Number of bytes of gRPC message data (may be zero), which follows
        // the trailing metadata.
        int messageBytes = 0;

        // OR-ed combination of various flag bits; see below for definitions.
        byte flags = 0;

        // Currently supported flag bits:

        // Indicates that this message contains all available initial metadata
        // (possibly none).
        static final byte initMdPresent = 1;

        // Indicates that, as of this Homa RPC, all message data has been sent.
        // If the message data is too long to fit in a single message, only the
        // last message has this bit set.
        static final byte messageComplete = 2;

        // Indicates that this message contains all available trailing metadata
        // (possibly none).
        static final byte trailMdPresent = 4;

        // Indicates that this message is a Homa request (meaning it that it
        // requires an eventual response).
        static final byte isRequest = 8;

        // Indicates that there is no useful information in this message;
        // it is a dummy Homa response sent by the other side.
        static final byte emptyResponse = 16;

        // Indicates that the sender has cancelled this RPC, and the receiver
        // should do the same.
        static final byte cancelled = 32;

        // Number of bytes occupied by a Header.
        static final int length = 21;

        /**
         * Construct a new Header
         * @param sid
         *      Identifer for the client stream this message belongs to.
         * @param sequence
         *      Sequence number of this message within the stream.
         * @param flags
         *      Initial value for the flags field.
         */
        Header(int sid, int sequence, byte flags) {
            this.sid = sid;
            this.sequence = sequence;
            initMdBytes = 0;
            trailMdBytes = 0;
            messageBytes = 0;
            this.flags = flags;
        }

        /**
         * Construct a new Header, leaving fields undefined (intended for
         * incoming messages).
         */
        Header() {
        }

        /**
         * Serializes the contents of this object into an outgoing message.
         * @param buf
         *      Used to build and transmit the outgoing message; must have
         *      big-endian byte order. The header will be written at the
         *      current position in the buffer (presumably the beginning?)
         */
        void serialize(ByteBuffer buf) {
            // Note: the order here must match the order of fields in
            // the C++ header wire.h.
            buf.putInt(sid);
            System.out.printf("serialized streamId %d in header\n", sid);
            buf.putInt(sequence);
            buf.putInt(initMdBytes);
            buf.putInt(trailMdBytes);
            buf.putInt(messageBytes);
            buf.put(flags);
        }

        /**
         * Extracts header information from incoming Homa message.
         * @param buf
         *      Holds the contents of an incoming message. Buf will be
         *      positioned at the first byte of data after the header
         *      when this method returns.
         */
        void deserialize(ByteBuffer buf) {
            buf.position(0);
            sid = buf.getInt();
            sequence = buf.getInt();
            initMdBytes = buf.getInt();
            trailMdBytes = buf.getInt();
            messageBytes = buf.getInt();
            flags = buf.get();
        }
    }

    /**
     * Serialize metadata values into an outgoing Homa message.
     * @param md
     *      Metadata to serialize; contains an even number of elements,
     *      consisting of alternating keys and values.
     * @param buf
     *      Used to build and transmit the outgoing message; must have
     *      big-endian byte order and enough space to hold the metadata.
     *      The metadata is written at the current position in the buffer
     *      (and that position is advanced).
     * @return
     *      The total number of bytes of data added to buf.
     */
    static int serializeMetadata(byte[][] md, ByteBuffer buf) {
        // See Mdata definition in wire.h (C++) for wire format of metadata.
        int totalBytes = 0;

        for (int i = 0; i < md.length; i+= 2) {
            buf.putInt(md[i].length);
            buf.putInt(md[i+1].length);
            buf.put(md[i]);
            buf.put(md[i+1]);
            totalBytes += 8 + md[i].length + md[i+1].length;
        }
        return totalBytes;
    }

    /**
     * Extract metadata from an incoming Homa message.
     * @param buf
     *      Contains the raw message. Must be positioned at the first byte of
     *      metadata within the message. Upon return, the position will be
     *      the first byte of data after the metadata.
     * @param numBytes
     *      Total bytes of metadata in the message.
     * @return
     *      The extracted metadata, in a form suitable for passing to gRPC.
     */
    static Metadata deserializeMetadata(ByteBuffer buf, int numBytes) {
        // See Mdata definition in wire.h (C++) for wire format of metadata.
        ArrayList<byte[]> keysAndValues = new ArrayList();
        int bytesLeft = numBytes;
        while (bytesLeft > 0) {
            int keyLength = buf.getInt();
            int valueLength = buf.getInt();
            if (bytesLeft < (8 + keyLength + valueLength)) {
                System.out.printf("Bogus incoming metadata\n");
                break;
            }
            byte[] key = new byte[keyLength];
            for (int i = 0; i < keyLength; i++) {
                key[i] = buf.get();
            }
            keysAndValues.add(key);
            byte[] value = new byte[valueLength];
            for (int i = 0; i < valueLength; i++) {
                value[i] = buf.get();
            }
            keysAndValues.add(value);
            bytesLeft = bytesLeft - keyLength - valueLength - 8;
        }
        return HomaMetadata.newMetadata(keysAndValues.size()/2,
                keysAndValues.toArray());
    }
}
