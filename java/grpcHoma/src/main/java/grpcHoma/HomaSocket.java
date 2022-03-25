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

import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.net.*;

import cz.adamh.utils.NativeUtils;

/**
 * An instance of this class represents a socket over which Homa RPCs
 * can be sent and received.
 */
public class HomaSocket {

    /**
     * Thrown when serious problems occur related to Homa (these
     * errors are probably fatal).
     */
    protected static class HomaError extends Error {
        /**
         * Construct a HomaError.
         * @param messageFormat
         *      Used to generate a human-readable description of the
         *      problem (passed to String.format along with all of
         *      the additional arguments).
         * @param arguments
         */
        public HomaError(String messageFormat, Object... arguments) {
            super(String.format(messageFormat, arguments));
        }
    }
    
    /**
     * This class contains information that uniquely describes an RPC.
     * Intended mostly for communication with JNI code, but applications
     * can access the information contained here. 
     */
    static public class RpcSpec {
        // Used as shared memory with JNI code; contains the following info:
        // Peer IP address (4 bytes, network byte order)
        // Peer port number (4 bytes)
        // RPC id (8 bytes)
        // Total message length (4 bytes)
        // Any changes to the structure above must be reflected in the
        // Spec class in homa.cpp, and vice versa.
        ByteBuffer spec;
        
        // Offsets within spec:
        protected static final int ipOffset = 0;
        protected static final int portOffset = 4;
        protected static final int idOffset = 8;
        protected static final int lengthOffset = 16;
        
        protected static final int length = 20;
        
        /**
         * Construct an RpcSpec.
         * @param dest
         *      Where to send the RPC.
         */
        public RpcSpec(InetSocketAddress dest) {
            spec = ByteBuffer.allocateDirect(length);
            spec.order(ByteOrder.nativeOrder());
            byte[] addr = dest.getAddress().getAddress();
            if (addr.length != 4) {
                throw new HomaError("Invalid address for RpcSpec: had %d "
                        + "bytes (expected 4)", addr.length);
            }
            spec.put(addr, ipOffset, 4);
            spec.putInt(portOffset, dest.getPort());
            spec.putLong(idOffset, 0);
        }
        
        /**
         * Construct an undefined RpcSpec.
         */
        public RpcSpec() {
            spec = ByteBuffer.allocateDirect(length);
            spec.order(ByteOrder.nativeOrder());
            spec.putInt(ipOffset, 0);
            spec.putInt(portOffset, 0);
            spec.putLong(idOffset, 0);
        }
        
        /**
         * Return the RPC id from an RpcSpec.
         */
        public long getId() {
            return spec.getLong(idOffset);
        }

        /**
         * Return the network address contained in an RpcSpec.
         */
        public InetSocketAddress getInetSocketAddress() {
            byte[] addr = new byte[4];
            for (int i = 0; i < 4; i++) {
                addr[i] = spec.get(ipOffset+i);
            }
            try {
                return new InetSocketAddress(InetAddress.getByAddress(addr),
                        spec.getInt(portOffset));
            } catch (UnknownHostException e) {
                // Shouldn't ever happen, just return garbage.
                return new InetSocketAddress(spec.getInt(portOffset));
            }
        }
        
        /**
         * Reset an RpcSpec to its initial (undefined) state.
         */
        public void reset() {
            spec.putInt(ipOffset, 0);
            spec.putInt(portOffset, 0);
            spec.putLong(idOffset, 0);
        }
    }
    
    /**
     * Constructs a HomaSocket, which will use an automatically generated
     * port number.
     */
    public HomaSocket() {
        if (!jniLoaded) {
            loadJNI();
        }
        fd = socketNative(0);
        if (fd < 0) {
            throw new HomaError("Couldn't create Homa socket: %s",
                    strerror(-fd));
        }
    }
  
    /**
     * Constructs a HomaSocket that uses a specific well-defined port.
     * @param port
     *      Number of port to use; must be less than 32768.
     */
    public HomaSocket(int port) {
        if (!jniLoaded) {
            loadJNI();
        }
        fd = socketNative(port);
        if (fd < 0) {
            throw new HomaError("Couldn't create Homa socket for port %d: %s",
                    port, strerror(-fd));
        }
    }
    
    /**
     * Close the Linux socket associated with this object. The object
     * should not be used anymore after invoking this method.
     */
    public void close() {
        if (fd > 0) {
            closeNative(fd);
            fd = -1;
        }
    }
    
    /**
     * Issue a new RPC request. This method is asynchronous: it returns
     * before the request message has actually been sent or received.
     * @param spec
     *      Identifies the peer host and port of the server that will
     *      handle the RPC; the id field will be filled in with the
     *      id assigned to this RPC.
     * @param buffer
     *      The request message to send. Must be a direct buffer.
     * @return
     *      Returns the id for the new RPC, or a negative errno if an
     *      error prevented the message from being sent.
     */
    public long send(RpcSpec spec, ByteBuffer buffer) {
        return sendNative(fd, buffer, buffer.position(), spec.spec);
    }
    
    // Flag values for @receive (see homa_recv man page for documentation):
    public static final int flagReceiveRequest = 1;
    public static final int flagReceiveResponse = 2;
    public static final int flagReceiveNonblocking = 4;
    public static final int flagReceivePartial = 8;
    
    /**
     * Receive an RPC request or response.
     * @param buffer
     *      The message will be returned here, and must fit within the
     *      existing length of the buffer. The buffer's position will
     *      be set to 0, and its limit to the amount of data returned.
     *      This must be a direct buffer.
     * @param flags
     *      Various flag bits; see definitions above for details.
     * @param spec
     *      If the contents are not in the undefined state, then a
     *      message for this RPC may be returned regardless of the
     *      bits set in @flags. Otherwise, only RPCs specified by
     *      @flags will be returned. Contents will be overwritten
     *      with information about the RPC of the returned message;
     *      must be passed to @reply.
     * @return
     *      The total length of the incoming message (even if there wasn't
     *      room in @buffer to return all of it), or a negative errno
     *      value if there was an error.
     */
    public int receive(ByteBuffer buffer, int flags, RpcSpec spec) {
        int result = receiveNative(fd, flags, buffer, buffer.limit(),
                spec.spec);
        if (result > 0) {
            buffer.clear();
            buffer.limit(spec.spec.getInt(RpcSpec.lengthOffset));
        }
        return result;
    }
    
    /**
     * Respond to a previously received request.
     * @param spec
     *      RPC that is being responded to: must have been returned by
     *      a previous call to receive.
     * @param buffer
     *      The contents of the response message. Must be a direct buffer.
     * @return
     *      Either zero or a negative errno value.
     */
    public int reply(RpcSpec spec, ByteBuffer buffer) {
        return replyNative(fd, buffer, buffer.position(), spec.spec);
    }
    
    /**
     * Returns a human-readable string describing an errno error code
     * (the same that the C function strerror would return).
     * @param errno
     *      An errno value.
     * @return
     *      The string corresponding to @errno.
     */
    public static String strerror(int errno) {
        if (!jniLoaded) {
            loadJNI();
        }
        return strerrorNative(errno);
    }
    
    /**
     * Makes sure that the JNI native library needed by this class
     * has been loaded.
     */
    private synchronized static void loadJNI() {
        if (jniLoaded) {
            return;
        }
        String jarPath = System.getProperty("grpcHoma.libhomaJni.so");
        try {
            if (jarPath != null) {
                // Need this during unit tests, when this class is run
                // from a class file, not from a .jar that also contains
                // the JNI methods.
                jarPath = System.getProperty("user.dir") + "/" + jarPath;
                System.load(jarPath);
            } else {
                NativeUtils.loadLibraryFromJar("/libhomaJni.so");
            }
        } catch (IOException e) {
            if (jarPath == null) {
                try {
                    jarPath = HomaSocket.class.getProtectionDomain()
                            .getCodeSource().getLocation().toURI().getPath();
                } catch (Exception pathErr) {
                    jarPath = "<unknown>";
                }
            }
            throw new HomaError("Couldn't load libhomaJni.so from %s: %s",
                    jarPath, e.getMessage());
        }
        jniLoaded = true;
    }
    
    // True means the JNI module has been successfully loaded.
    static boolean jniLoaded = false;
    
    // Unix file descriptor for this socket. -1 means this socket
    // has been closed.
    int fd;
    
    // JNI methods used internally to manage Homa sockets.
    public static native int closeNative(int fd);
    private static native int receiveNative(int fd, int flags,
            ByteBuffer buffer, int length, ByteBuffer spec);
    private static native int replyNative(int fd, ByteBuffer buffer,
            int length, ByteBuffer spec);
    private static native long sendNative(int fd, ByteBuffer buffer,
            int length, ByteBuffer spec);
    public static native int socketNative(int port);
    private static native String strerrorNative(int errno);
}