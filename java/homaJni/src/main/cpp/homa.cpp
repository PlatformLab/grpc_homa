/*
 * This file implements JNI functions for invoking Homa kernel calls.
 * It can be accessed from Java through the HomaSocket class; the APIs
 * are all documented in that class.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <jni.h>

#include "homa.h"

/**
 * This class defines the layout of the @spec ByteBuffer in Java RpcSpecs.
 * Structural changes here must be reflected in HomaSocket.RpcSpec
 * and vice versa.
 */
struct Spec {
    // Peer address, network byte order.
    int addr;
    
    // Port number on peer.
    int port;
    
    // RPC sequence number.
    uint64_t id;
    
    // Number of bytes received in the most recent call to receive.
    int length;
};

/**
 * JNI implementation of HomaSocket.socketNative: opens a socket.
 * \param env
 *      Used to access information in the Java environment (not used).
 * \param jHomaSocket.
 *      Info about the HomaSocket class from which this method was invoked
 *      (not used).
 * \param jPort
 *      Port number to use for the socket; 0 means that Homa should
 *      assign a number automatically.
 * \return
 *      The fd assigned to the socket, or a negative errno value.
 */
extern "C" JNIEXPORT jint Java_grpcHoma_HomaSocket_socketNative(JNIEnv *env,
        jclass jHomaSocket, jint jPort) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA);
    if ((fd >= 0) && (jPort > 0)) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(jPort);
        if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr),
                sizeof(addr)) != 0) {
            return -errno;
        }
    }
    return fd;
}

/**
 * JNI implementation of HomaSocket.closeNative: closes a socket.
 * \param env
 *      Used to access information in the Java environment (not used).
 * \param jHomaSocket.
 *      Info about the HomaSocket class from which this method was invoked
 *      (not used).
 * \param fFd
 *      Homa socket to close.
 */
extern "C" JNIEXPORT void Java_grpcHoma_HomaSocket_closeNative(JNIEnv *env,
        jclass jHomaSocket, jint jFd) {
    close(jFd);
}

/**
 * JNI implementation of HomaSocket.strerror: return a human-readable
 * string for a given errno.
 * \param env
 *      Used to access information in the Java environment (not used).
 * \param jHomaSocket.
 *      Info about the HomaSocket class from which this method was invoked
 *      (not used).
 * \param jErrno
 *      Errno value returned by some other method.
 */
extern "C" JNIEXPORT jstring Java_grpcHoma_HomaSocket_strerrorNative(
        JNIEnv *env, jclass jHomaSocket, jint jErrno) {
    return env->NewStringUTF(strerror(jErrno));
}

/**
 * JNI implementation of HomaSocket.sendNative: sends the request
 * message for a new outgoing Homa RPC.
 * \param env
 *      Used to access information in the Java environment (not used).
 * \param jHomaSocket.
 *      Info about the HomaSocket class from which this method was invoked
 *      (not used).
 * \param fFd
 *      Homa socket to use for sending the message (previous return value
 *      from socket).
 * \param jBuffer
 *      ByteBuffer containing the contents of the message (none of the
 *      metadata for this buffer is modified).
 * \param jLength
 *      Number of bytes in the message in jBuffer.
 * \param jSpec
 *      Reference to the a HomaSpec.spec ByteBuffer: address and port
 *      identify the target for the RPC, and the id will be filled in
 *      by this method.
 * \return
 *      Returns zero for success or a negative errno value if there
 *      was an error.
 */
extern "C" JNIEXPORT jlong Java_grpcHoma_HomaSocket_sendNative(JNIEnv *env,
        jclass jHomaSocket, jint jFd, jobject jBuffer, jint jLength,
        jobject jSpec) {
    void *buffer = env->GetDirectBufferAddress(jBuffer);
    Spec *spec = static_cast<Spec *>(env->GetDirectBufferAddress(jSpec));
    if ((buffer == NULL) || (spec == NULL)) {
        return -EINVAL;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = spec->addr;
    addr.sin_port = htons(spec->port);
    
    struct sockaddr *saddr = reinterpret_cast<struct sockaddr *> (&addr);
    if (homa_send(jFd, buffer, jLength, saddr, sizeof(addr), &spec->id)
            == 0) {
        return 0;
    } else {
        return -errno;
    }
}

/**
 * JNI implementation of HomaSocket.replyNative: sends the response
 * message for an RPC.
 * \param env
 *      Used to access information in the Java environment (not used).
 * \param jHomaSocket.
 *      Info about the HomaSocket class from which this method was invoked
 *      (not used).
 * \param fFd
 *      Homa socket to use for sending the response (previous return value
 *      from socket).
 * \param jBuffer
 *      ByteBuffer containing the response message (none of the metadata
 *      for this buffer is modified).
 * \param jLength
 *      Number of bytes in the message in jBuffer.
 * \param jSpec
 *      Reference to the a HomaSpec.spec ByteBuffer: identifies the client
 *      that issued the RPC (as set by receiveNative).
 * \return
 *      Returns zero for success or a negative errno value if there
 *      was an error.
 */
extern "C" JNIEXPORT jlong Java_grpcHoma_HomaSocket_replyNative(JNIEnv *env,
        jclass jHomaSocket, jint jFd, jobject jBuffer, jint jLength,
        jobject jSpec) {
    void *buffer = env->GetDirectBufferAddress(jBuffer);
    Spec *spec = static_cast<Spec *>(env->GetDirectBufferAddress(jSpec));
    if ((buffer == NULL) || (spec == NULL)) {
        return -EINVAL;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = spec->addr;
    addr.sin_port = htons(spec->port);
    
    struct sockaddr *saddr = reinterpret_cast<struct sockaddr *> (&addr);
    if (homa_reply(jFd, buffer, jLength, saddr, sizeof(addr), spec->id)
            == 0) {
        return 0;
    } else {
        return -errno;
    }
}

/**
 * JNI implementation of HomaSocket.receiveNative: receives an incoming
 * message.
 * \param env
 *      Used to access information in the Java environment (not used).
 * \param jHomaSocket.
 *      Info about the HomaSocket class from which this method was invoked
 *      (not used).
 * \param fFd
 *      Homa socket to use for receiving the message (previous return value
 *      from socket).
 * \param flags
 *      OR-ed collection of bits that control receive operation; same
 *      as the @flag passed to homa_recv.
 * \param jBuffer
 *      ByteBuffer in which to store the incoming message (none of the
 *      metadata for this buffer is modified).
 * \param jLength
 *      Number of bytes available in jBuffer.
 * \param jSpec
 *      Reference to the a HomaSpec.spec ByteBuffer: if the id in this
 *      structure is nonzero, then this structure describes a specific
 *      RPC, which may be received even if not selected by @flags.
 *      This will be modified to describe the incoming message (including
 *      the length field).
 * \return
 *      The total number of bytes in the message (may be more than the
 *      number actually received), or a negative errno value if there
 *      was an error.
 */
extern "C" JNIEXPORT jlong Java_grpcHoma_HomaSocket_receiveNative(JNIEnv *env,
        jclass jHomaSocket, jint jFd, jint jFlags, jobject jBuffer,
        jint jLength, jobject jSpec) {
    void *buffer = env->GetDirectBufferAddress(jBuffer);
    Spec *spec = static_cast<Spec *>(env->GetDirectBufferAddress(jSpec));
    if ((buffer == NULL) || (spec == NULL)) {
        return -EINVAL;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = spec->addr;
    addr.sin_port = ntohs(spec->port);
    size_t addrLength = sizeof(addr);
    size_t msgLength;
    
    struct sockaddr *saddr = reinterpret_cast<struct sockaddr *> (&addr);
    int result = homa_recv(jFd, buffer, jLength, jFlags, saddr, &addrLength,
            &spec->id, &msgLength);
    if (result < 0) {
        return -errno;
    }
    spec->addr = addr.sin_addr.s_addr;
    spec->port = ntohs(addr.sin_port);
    spec->length = result;
    return msgLength;
}
