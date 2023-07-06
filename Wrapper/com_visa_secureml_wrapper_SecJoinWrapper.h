/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class SecJoinWrapper */

#ifndef _Included_SecJoinWrapper
#define _Included_SecJoinWrapper
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     SecJoinWrapper
 * Method:    join
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT jlong JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_init
  (JNIEnv *, jobject, jstring, jstring, jstring, jstring, jint);

JNIEXPORT jbyteArray JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_runJoin
  (JNIEnv *, jobject, jlong, jbyteArray, jlong);

JNIEXPORT void JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_releaseState
  (JNIEnv *, jobject, jlong);

JNIEXPORT jboolean JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_isProtocolReady
  (JNIEnv *, jobject, jlong);

JNIEXPORT void JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_getIntersection
(JNIEnv *, jobject, jlong, jstring, jstring);

#ifdef __cplusplus
}
#endif
#endif
