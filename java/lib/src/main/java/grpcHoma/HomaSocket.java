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

import cz.adamh.utils.NativeUtils;

/**
 * An instance of this class represents a socket over which Homa RPCs
 * can be sent and received.
 */
public class HomaSocket {
    
    // This loads the binary containing JNI support methods, which must
    // be in the same .jar file as this class.
    static {
        try {
            NativeUtils.loadLibraryFromJar("/libhomaJni.so");   
        } catch (IOException e) {
            String jarPath;
            try {
                jarPath = HomaSocket.class.getProtectionDomain()
                        .getCodeSource().getLocation().toURI().getPath();
            } catch (Exception pathErr) {
                jarPath = "<unknown>";
            }
            loadError = String.format(
                    "HomaSocket couldn't load libhomaJni.so from %s: %s",
                    jarPath, e.getMessage());
        }
    }
    
    /**
     * Constructs a HomaSocket, which will use an automatically generated
     * port number.
     */
    public HomaSocket() {
        if (loadError != null) {
            throw new RuntimeException(loadError);
        }
        fd = socket(0);
        System.out.printf("socket returned %d\n", fd);
    }
  
    /**
     * Constructs a HomaSocket that uses a specific well-defined port.
     * @param port
     *      Number of port to use; must be less than 32768.
     */
    public HomaSocket(int port) {
        if (loadError != null) {
            throw new RuntimeException(loadError);
        }
        fd = socket(port);
        System.out.printf("socket(%d) returned %d\n", port, fd);
    }

    // If the JNI binary couldn't be loaded, this will contain a message
    // describing the problem; otherwise it will be null.
    static String loadError;
    
    // Unix file descriptor for this socket.
    int fd;
    
    /**
     * JNI method to invoke the @socket system call to create a Homa
     * socket.
     * @param port
     *      Port number to use for the socket, or 0 to assign a port
     *      number automatically.
     * @return
     *      The Linux fd assigned to this port, or a negative errno
     *      if there was an error.
     */
    private static native int socket(int port);
}