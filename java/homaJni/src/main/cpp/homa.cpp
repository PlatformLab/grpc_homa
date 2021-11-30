/*
 * This file implements JNI functions for invoking Homa kernel calls.
 * It can be accessed from Java through the "homa" class.
 */

#include <stdio.h>
#include <jni.h>


extern "C" JNIEXPORT jint Java_grpcHoma_HomaSocket_socket(JNIEnv *env, jclass jHoma, jint port) {
    printf("JNI received port value of %d\n", port);
    return port+1;
}
