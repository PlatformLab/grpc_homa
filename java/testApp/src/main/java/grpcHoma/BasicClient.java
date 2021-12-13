package basic;

import io.grpc.*;
import io.grpc.stub.StreamObserver;

/**
 * An instance of this class can be used to issue RPCs from basic.proto
 * to a particular target (server and port).
 */
public class BasicClient {
    public BasicClient(Channel channel) {
        stub = BasicGrpc.newBlockingStub(channel);
    }
    
    /**
     * Issue a simple RPC that sends a block of data to the server
     * and receives another block in response.
     * @param requestLength
     *      Number of bytes in the request message.
     * @param replyLength
     *      Number of bytes in the response message.
     * @throws InterruptedException 
     */
    public void ping(int requestLength, int replyLength)
            throws InterruptedException {
        BasicProto.Request.Builder builder = BasicProto.Request.newBuilder();
        builder.setRequestItems(requestLength);
        builder.setReplyItems(replyLength);
        for (int i = 0; i < requestLength; i++) {
                builder.addData(i);
        }
        try {
            BasicProto.Response response = stub.ping(builder.build());
            if (response.getDataCount() != replyLength) {
                System.out.printf("Ping returned %d bytes, expected %d\n",
                        response.getDataCount(), replyLength);
            }
        } catch (StatusRuntimeException e) {
            System.out.printf("Ping RPC failed: %s\n", e.getStatus());
            Thread.sleep(1000);
        }
    }
    
    protected BasicGrpc.BasicBlockingStub stub;
}