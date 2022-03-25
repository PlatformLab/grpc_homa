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

import io.grpc.ChannelLogger;

/**
 * A single instance of this class is used to store state shared across
 * all RPCs emanating from a client process.
 */
public class HomaClient {
    // Used for all Homa communication.
    HomaSocket homa;

    // Singleton instance, shared across all transports and streams.
    static HomaClient instance;

    // Identifier to use for next client stream.
    int nextId;

    // Receives responses.
    ClientThread receiver;

    // Used for logging various messages.
    ChannelLogger logger;

    /**
     * Constructor for HomaClients.
     * @param logger
     *      Use this for logging messages.
     */
    HomaClient(ChannelLogger logger) {
        homa = new HomaSocket();
        nextId = 1;
        receiver = new ClientThread(this);
        this.logger = logger;
        receiver.start();
    }

    /**
     * Returns the singleton instance (creates it if it doesn't already exist).
     * @param logger
     *     If a new HomaClient is created, this will be used by the HomaClient
     *     for logging during its lifetime.
     */
    static HomaClient getInstance(ChannelLogger logger) {
        if (instance == null) {
            synchronized (HomaClient.class) {
                // Must recheck instance again, since it could have been
                // created while we were waiting for the lock.
                if (instance == null) {
                    instance = new HomaClient(logger);
                }
            }
        }
        return instance;
    }

    /**
     * This class represents a thread that receives incoming Homa responses
     * and handles them appropriately.
     */
    static class ClientThread extends Thread {
        // Shared client state.
        HomaClient client;

        ClientThread(HomaClient client) {
            this.client = client;
        }

        public void run() {
            try {
                while (true) {
                    HomaIncoming msg = new HomaIncoming();
                    int err = msg.read(client.homa, HomaSocket.flagReceiveResponse);
                    if (err != 0) {
                        System.out.printf("Error receiving Homa message: %s\n",
                                HomaSocket.strerror(err));
                        continue;
                    }
                    System.out.printf("Received response for id %d from %s, " +
                            "totalBytes %d, initMdBytes %d, messageBytes %d, " +
                            "trailMdBytes %d, flags 0x%x\n",
                            msg.homaId, msg.peer.getInetSocketAddress(),
                            msg.length, msg.header.initMdBytes,
                            msg.header.messageBytes, msg.header.trailMdBytes,
                            msg.header.flags);
                }
            } catch (Exception e) {
                String message = e.getMessage();
                if (message == null) {
                    message = "no information about cause";
                }
                System.out.printf("HomaClient.ClientThread crashed: %s\n",
                        message);
            }
        }
    }
}