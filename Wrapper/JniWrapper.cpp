#include "com_visa_secureml_wrapper_SecJoinWrapper.h"
#include "CppWrapper.h"
// #include "state.h"

// #ifndef SECUREJOIN_ENABLE_JNI
// static_assert(1, "SECUREJOIN_ENABLE_JNI not defined");
// #endif

JNIEXPORT void JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_testApi
(JNIEnv* env, jobject obj)
{
  std::string temp("Hello Word");
  secJoin::testApi(temp);
}

JNIEXPORT jlong JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_initState
(JNIEnv* env, jobject obj, jstring csvPath, jstring visaMetaDataPath, jstring clientMetaDataPath,
  jobjectArray jstringArr, jintArray jintArr, jboolean isUnique, jboolean verbose, 
  jboolean mock, jboolean debug)
{
  std::string cppCSVPath = env->GetStringUTFChars(csvPath, NULL);
  std::string cppVisaMetaDataPath = env->GetStringUTFChars(visaMetaDataPath, NULL);
  std::string cppClientMetaDataPath = env->GetStringUTFChars(clientMetaDataPath, NULL);

  // Getting Literals 
  // Code Reference https://stackoverflow.com/questions/19591873/get-an-array-of-strings-from-java-to-c-jni
  std::vector<std::string> literals;

  // Get length
  int len = env->GetArrayLength(jstringArr);

  for (int i=0; i<len; i++) {
      // Cast array element to string
      jstring jstr = (jstring) (env->GetObjectArrayElement(jstringArr, i));

      // Convert Java string to std::string
      const jsize strLen = env->GetStringUTFLength(jstr);
      const char *charBuffer = env->GetStringUTFChars(jstr, (jboolean *) 0);
      std::string str(charBuffer, strLen);

      // Push back string to vector
      literals.push_back(str);

      // Release memory
      env->ReleaseStringUTFChars(jstr, charBuffer);
      env->DeleteLocalRef(jstr);
  }
  
  // Getting the opInfo
  len = env->GetArrayLength(jintArr);
  jint *ptr = env->GetIntArrayElements(jintArr, 0);
  std::vector<oc::i64> opInfo(ptr, ptr + len);

  static_assert(sizeof(jlong) == sizeof(secJoin::State*), "jlong must be pointer size");
  return (jlong)secJoin::initState(cppCSVPath, cppVisaMetaDataPath, cppClientMetaDataPath, literals,
     opInfo, isUnique, verbose, mock, debug);
}

JNIEXPORT jbyteArray JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_runProtocol
(JNIEnv* env, jobject obj, jlong stateAddress, jbyteArray data, jlong dataSize)
{

  // Get elements of the array
  jbyte* elements = env->GetByteArrayElements(data, 0);

  std::vector<oc::u8> buff(dataSize);
  memcpy(buff.data(), elements, dataSize);

  auto b = secJoin::runProtocol((secJoin::State*)stateAddress, buff);

  jbyteArray byteArray = (*env).NewByteArray(b.size());
  (*env).SetByteArrayRegion(byteArray, 0, b.size(), reinterpret_cast<const signed char*>(b.data()));
  // std::cout << "In the C code, the size of byte array is " << b.size() << std::endl;
  return byteArray;
}



JNIEXPORT void JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_releaseState
(JNIEnv* env, jobject obj, jlong memoryAddress)
{
  secJoin::releaseState((secJoin::State*)memoryAddress);
}


JNIEXPORT jboolean JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_isProtocolReady
(JNIEnv* env, jobject obj, jlong stateAddress)
{

  return secJoin::isProtocolReady((secJoin::State*)stateAddress);
}

JNIEXPORT void JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_getOtherShare
(JNIEnv* env, jobject obj, jlong stateAddress, jboolean isUnique, jboolean isAgg)
{
  secJoin::getOtherShare((secJoin::State*)stateAddress, isUnique, isAgg);
}



JNIEXPORT void JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_getJoinTable
(JNIEnv* env, jobject obj, jlong stateAddress, jstring csvPath,
  jstring metaDataPath, jboolean isUnique)
{

  std::string cppCSVPath = env->GetStringUTFChars(csvPath, NULL);
  std::string cppMetaPath = env->GetStringUTFChars(metaDataPath, NULL);

  secJoin::getJoinTable((secJoin::State*)stateAddress, cppCSVPath, cppMetaPath, isUnique);
}


JNIEXPORT void JNICALL Java_com_visa_secureml_wrapper_SecJoinWrapper_aggFunc
(JNIEnv* env, jobject obj, jlong stateAddress)
{
  secJoin::aggFunc((secJoin::State*)stateAddress);
}