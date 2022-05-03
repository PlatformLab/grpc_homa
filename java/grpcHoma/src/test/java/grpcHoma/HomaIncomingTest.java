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
import java.net.*;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class HomaIncomingTest {
    HomaIncoming msg;

    @BeforeEach
    void setUp() {
        msg = new HomaIncoming();
    }

    @Test
    public void test_HomaIncoming_getBuffer_fromInitialData() {
        msg.initialPayload.put((byte) 1);
        msg.initialPayload.put((byte) 2);
        msg.initialPayload.putInt(12345);
        msg.initialPayload.flip();
        msg.initialPayload.position(2);
        ByteBuffer b = msg.getBuffer(4);
        assertEquals(b, msg.initialPayload);
        assertEquals(12345, b.getInt());
        assertEquals(0, msg.initialPayload.remaining());
    }
    @Test
    public void test_HomaIncoming_getBuffer_fromTail() {
        msg.initialPayload.limit(0);
        msg.tail = ByteBuffer.allocate(100);
        msg.tail.order(ByteOrder.BIG_ENDIAN);
        msg.tail.put((byte) 1);
        msg.tail.put((byte) 2);
        msg.tail.putInt(12345);
        msg.tail.put((byte) 3);
        msg.tail.flip();
        msg.tail.position(2);
        ByteBuffer b = msg.getBuffer(4);
        assertEquals(b, msg.tail);
        assertEquals(12345, b.getInt());
        assertEquals(1, msg.tail.remaining());
    }
    @Test
    public void test_HomaIncoming_getBuffer_mustSplice() {
        msg.initialPayload.put((byte) 1);
        msg.initialPayload.put((byte) 2);
        msg.initialPayload.putInt(12345);
        msg.initialPayload.flip();
        msg.tail = ByteBuffer.allocate(100);
        msg.tail.order(ByteOrder.BIG_ENDIAN);
        msg.tail.put((byte) 1);
        msg.tail.put((byte) 2);
        msg.tail.putInt(12345);
        msg.tail.flip();
        msg.initialPayload.position(2);
        ByteBuffer b = msg.getBuffer(6);
        assertNotSame(b, msg.initialPayload);
        assertNotSame(b, msg.tail);
        assertEquals(12345, b.getInt());
        assertEquals(1, b.get());
        assertEquals(2, b.get());
        assertEquals(0, msg.initialPayload.remaining());
        assertEquals(4, msg.tail.remaining());
    }
}
