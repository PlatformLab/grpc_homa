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

    /**
     * Constructor for HomaClients.
     */
    HomaClient() {
        homa = new HomaSocket();
        nextId = 1;
    }

    /**
     * Returns the singleton instance (creates it if it doesn't already exist).
     */
    static HomaClient getInstance() {
        if (instance == null) {
            synchronized (HomaClient.class) {
                // Must recheck instance again, since it could have been
                // created while we were waiting for the lock.
                if (instance == null) {
                    instance = new HomaClient();
                }
            }
        }
        return instance;
    }
}