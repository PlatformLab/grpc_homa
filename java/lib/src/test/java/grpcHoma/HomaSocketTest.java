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
import java.net.*;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class HomaSocketTest {
    HomaSocket serverSock;
    HomaSocket clientSock;
    
    @AfterEach
    void tearDown() {
        if (serverSock != null) {
            serverSock.close();
            serverSock = null;
        }
        if (clientSock != null) {
            clientSock.close();
            clientSock = null;
        }
    }

    @Test
    public void test_HomaSocket_constructor__bad_port() {
        HomaSocket.HomaError error = assertThrows(
                HomaSocket.HomaError.class, () -> {
            new HomaSocket(100000);
        });
        assertEquals("Couldn't create Homa socket for port 100000: "
                + "Invalid argument", error.getMessage());
    }

    @Test
    public void test_HomaSocket__basic_roundtrip()
            throws UnknownHostException {
        serverSock = new HomaSocket(5555);
        clientSock = new HomaSocket(0);
        HomaSocket.RpcSpec outgoingSpec = new HomaSocket.RpcSpec(
                InetAddress.getByName("localhost"), 5555);
        HomaSocket.RpcSpec incomingSpec = new HomaSocket.RpcSpec();
        ByteBuffer clientBuffer = ByteBuffer.allocateDirect(100);
        ByteBuffer serverBuffer = ByteBuffer.allocateDirect(100);
        clientBuffer.putInt(11111);
        clientBuffer.putInt(22222);
        
        assertEquals(0, clientSock.send(outgoingSpec, clientBuffer));
        int result = serverSock.receive(serverBuffer,
                HomaSocket.flagReceiveRequest, incomingSpec);
        assertEquals(8, result);
        assertEquals(11111, serverBuffer.getInt(0));
        assertEquals(22222, serverBuffer.getInt(4));
        
        serverBuffer.clear();
        serverBuffer.putInt(3333);
        serverBuffer.putInt(4444);
        serverBuffer.putInt(5555);
        assertEquals(0, serverSock.reply(incomingSpec, serverBuffer));
        result = clientSock.receive(clientBuffer, 0, outgoingSpec);
        assertEquals(12, result);
        assertEquals(3333, clientBuffer.getInt());
        assertEquals(4444, clientBuffer.getInt());
        assertEquals(5555, clientBuffer.getInt());
    }

    @Test
    public void test_HomaSocket_send_to_unconnected_port()
            throws UnknownHostException {
        clientSock = new HomaSocket(0);
        HomaSocket.RpcSpec spec = new HomaSocket.RpcSpec(
                InetAddress.getByName("localhost"), 9876);
        ByteBuffer buffer = ByteBuffer.allocateDirect(100);
        buffer.putInt(12345);
        
        clientSock.send(spec, buffer);
        spec.reset();
        int result = clientSock.receive(buffer,
                HomaSocket.flagReceiveResponse, spec);
        assertEquals("Transport endpoint is not connected",
                HomaSocket.strerror(-result));
    }


    @Test
    public void test_HomaSocket_receive_bogus_id()
            throws UnknownHostException {
        clientSock = new HomaSocket(0);
        HomaSocket.RpcSpec spec = new HomaSocket.RpcSpec(
                InetAddress.getByName("localhost"), 9876);
        ByteBuffer buffer = ByteBuffer.allocateDirect(100);
        buffer.putInt(12345);
        
        clientSock.send(spec, buffer);
        spec.spec.putLong(HomaSocket.RpcSpec.idOffset, spec.getId()+1);
        int result = clientSock.receive(buffer, 0, spec);
        assertEquals("Invalid argument", HomaSocket.strerror(-result));
    }

    @Test
    public void test_HomaSocket__receive_in_pieces()
            throws UnknownHostException {
        serverSock = new HomaSocket(5555);
        clientSock = new HomaSocket(0);
        HomaSocket.RpcSpec outgoingSpec = new HomaSocket.RpcSpec(
                InetAddress.getByName("localhost"), 5555);
        ByteBuffer sendBuffer = ByteBuffer.allocateDirect(100);
        sendBuffer.putInt(11111);
        sendBuffer.putInt(22222);
        sendBuffer.putInt(33333);
        sendBuffer.putInt(44444);
        assertEquals(0, clientSock.send(outgoingSpec, sendBuffer));
        
        HomaSocket.RpcSpec incomingSpec = new HomaSocket.RpcSpec();
        ByteBuffer receiveBuffer = ByteBuffer.allocateDirect(100);
        receiveBuffer.limit(12);
        int result = serverSock.receive(receiveBuffer,
                HomaSocket.flagReceiveRequest|HomaSocket.flagReceivePartial,
                incomingSpec);
        assertEquals(16, result);
        assertEquals(12, receiveBuffer.limit());
        assertEquals(11111, receiveBuffer.getInt(0));
        assertEquals(22222, receiveBuffer.getInt(4));
        assertEquals(33333, receiveBuffer.getInt(8));
        
        receiveBuffer.clear();
        result = serverSock.receive(receiveBuffer, HomaSocket.flagReceivePartial,
                incomingSpec);
        assertEquals(16, result);
        assertEquals(4, receiveBuffer.limit());
        assertEquals(44444, receiveBuffer.getInt(0));
    }
    
    @Test
    public void test_strerror() {
        assertEquals("No such file or directory", HomaSocket.strerror(2));
        assertEquals("Broken pipe", HomaSocket.strerror(32));
    }
}