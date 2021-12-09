package grpcHoma;

import java.net.*;
import java.nio.ByteBuffer;
import java.util.Arrays;

/**
 * This file contains a main program for testing Java-based Homa
 * support for gRPC. Invoke it with the "--help" option for information
 * about how to use it.
 */

public class Main {

    static int firstServer = 1;
    static boolean isServer = false;
    static boolean useHoma = true;
    
    /**
     * Issues a small RPC to a server repeatedly, returns timing information.
     */
    static void testPing() {
        ByteBuffer buffer = ByteBuffer.allocateDirect(10000);
        
        if (isServer) {
            // We're implementing the server; just receive requests and
            // echo them back verbatim.
            System.out.printf("Server listening on port 4000\n");
            HomaSocket.RpcSpec spec = new HomaSocket.RpcSpec();
            HomaSocket server = new HomaSocket(4000);
            
            while (true) {
                spec.reset();
                int err = server.receive(buffer, HomaSocket.flagReceiveRequest,
                        spec);
                if (err < 0) {
                    System.out.printf("Error receiving request: %s\n",
                            HomaSocket.strerror(-err));
                    continue;
                }
                buffer.position(buffer.limit());
                err = server.reply(spec, buffer);
                if (err < 0) {
                    System.out.printf("Error responding to request: %s\n",
                            HomaSocket.strerror(-err));
                    continue;
                }
            }
        }
        
        // We are the client. Generate a series of small requests and
        // record timings.
        HomaSocket client = new HomaSocket(0);
        String serverName = String.format("node-%d", firstServer);
        InetAddress address;
        try {
            address = InetAddress.getByName(serverName);
        } catch (UnknownHostException e) {
            System.out.printf("Couldn't lookup host '%s'\n", serverName);
            return;
        }
        HomaSocket.RpcSpec spec = new HomaSocket.RpcSpec(address, 4000);
        int numPings = 10000;
        long rtts[] = new long[numPings];
        for (int i = -10; i < numPings; i++) {
            buffer.clear();
            for (int j = 1; j <= 10; j++) {
                buffer.putInt(j);
            }
            long start = System.nanoTime();
            long err = client.send(spec, buffer);
            if (err < 0) {
                System.out.printf("Error sending request %d: %s\n",
                        i, HomaSocket.strerror((int) -err));
                break;
            }
            err = client.receive(buffer, 0, spec);
            if (i >= 0) {
                rtts[i] = System.nanoTime() - start;
            }
            if (err < 0) {
                System.out.printf("Error receiving response %d: %s\n",
                        i, HomaSocket.strerror((int) -err));
                break;
            }
//            System.out.printf("Response has %d bytes (%d total bytes), "
//                    + "initial data %d %d %d %d\n",
//                    buffer.limit(), err, buffer.getInt(), buffer.getInt(),
//                    buffer.getInt(), buffer.getInt());
        }
        Arrays.sort(rtts);
        double min, p50, p99, max;
        min = rtts[0];
        p50 = rtts[numPings/2];
        p99 = rtts[99*numPings/100];
        max = rtts[numPings-1];
        System.out.printf("Ping results: min %.1f us, p50 %.1f us, "
                + "p99 %.1f us, max %.1f us", min*1e-3, p50*1e-3,
                p99*1e-3, max*1e-3);
    }
    
    /**
     * Converts a command-line word to an integer, throwing an exception
     * if the value can't be converted or there aren't enough words.
     * @param words
     *      Words of a command.
     * @param index
     *      Index of the desired option value (may not actually be present
     *      in @words).
     * @return 
     */
    static int parseInt(String words[], int index) {
        if (index >= words.length) {
            throw new RuntimeException(String.format(
                    "No value provided for %s option\n", words[index-1]));
        }
        try {
            return Integer.parseInt(words[index]);
        } catch (NumberFormatException e) {
            throw new RuntimeException(String.format(
                    "Bad value '%s' for %s option; must be integer\n",
                    words[index], words[index-1]));
        }
    }
    
    /**
     * Output brief information about how to use this program.
     */
    static void printHelp() {
        System.out.printf("Usage: prog [option option ...] test test ...\n\n");
        System.out.printf("This program is used to test Java support for "
                + "Homa in gRPC. Each of\n");
        System.out.printf("the test arguments specifies one test to run.\n");
        System.out.printf("The following command-line options are supported:\n");
        System.out.printf("    --first-server       Index of first server node"
                + "(default: 1)\n");
        System.out.printf("    --help               Print this message and "
                + "exit\n");
        System.out.printf("    --is-server          This node should act as "
                + "server (no argument,\n");
        System.out.printf("                         default: false)\n");
        System.out.printf("    --tcp                Use TCP for transport instead of Homa\n");
    }
    
    public static void main(String[] args) {
        int nextArg;
        
        for (nextArg = 0; nextArg < args.length; nextArg++) {
            String option = args[nextArg];
            if (!option.startsWith("--")) {
                break;
            }
            if (option.equals("--first-server")) {
                nextArg++;
                firstServer = parseInt(args, nextArg);
            } else if (option.equals("--help")) {
                printHelp();
                System.exit(0);
            } else if (option.equals("--is-server")) {
                isServer = true;
            } else if (option.equals("--tcp")) {
                useHoma = false;
            } else {
                System.out.printf("Unknown option %s\n", option);
                printHelp();
                System.exit(1);
            }
        }
        
        // Remaining arguments are all tested names.
        for ( ; nextArg < args.length; nextArg++) {
            String test = args[nextArg];
            if (test.equals("ping")) {
                testPing();
            } else {
                System.out.printf("Unknown test name '%s'; skipping\n", test);
            }
        }
    }
}
