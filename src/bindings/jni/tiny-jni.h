/*
 * Copyright (c) 1996, 2022, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/*
 * We used part of Netscape's Java Runtime Interface (JRI) as the starting
 * point of our design and implementation.
 */

/******************************************************************************
 * Java Runtime Interface
 * Copyright (c) 1996 Netscape Communications Corporation. All rights reserved.
 *****************************************************************************/

/*
 * tiny-jni by https://github.com/LekKit
 * This is a tiny & portable self-contained JNI API header,
 * allows to build JNI bridges in-tree without supplying JDK headers.
 * Just drop this in your sources & you're good to go.
 *
 * Original source from is from Oracle OpenJDK distribution.
 * The classpath exception allows to include this into non-GPL works.
 */

#ifndef _JAVASOFT_JNI_H_
#define _JAVASOFT_JNI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h> /* Use modern standard type definitions */
#include <stddef.h>
#include <stdarg.h>

#if defined(_WIN32)
#define JNIEXPORT __declspec(dllexport)
#define JNIIMPORT __declspec(dllimport)
#define JNICALL   __stdcall
#elif defined(__GNUC__)
#define JNIEXPORT __attribute__((visibility("default")))
#define JNIIMPORT __attribute__((visibility("default")))
#define JNICALL
#else
/*
 * Build the project anyways under unsupported target for JNI.
 * Here you might add your own attributes for porting
 */
#define JNIEXPORT
#define JNIIMPORT
#define JNICALL
#endif

#define JNI_FALSE 0
#define JNI_TRUE  1

#define JNI_OK        0    /* Success */
#define JNI_ERR       (-1) /* Unknown error */
#define JNI_EDETACHED (-2) /* Thread detached from the VM */
#define JNI_EVERSION  (-3) /* JNI version error */
#define JNI_ENOMEM    (-4) /* Not enough memory */
#define JNI_EEXIST    (-5) /* VM already created */
#define JNI_EINVAL    (-6) /* Invalid arguments */

#define JNI_COMMIT 1
#define JNI_ABORT  2

#define JDK1_2
#define JDK1_4

#define JNI_VERSION_1_1 0x00010001
#define JNI_VERSION_1_2 0x00010002
#define JNI_VERSION_1_4 0x00010004
#define JNI_VERSION_1_6 0x00010006
#define JNI_VERSION_1_8 0x00010008
#define JNI_VERSION_9   0x00090000
#define JNI_VERSION_10  0x000a0000
#define JNI_VERSION_19  0x00130000
#define JNI_VERSION_20  0x00140000

typedef uint8_t  jboolean;
typedef int8_t   jbyte;
typedef uint16_t jchar;
typedef int16_t  jshort;
typedef int32_t  jint;
typedef int32_t  jsize;
typedef int64_t  jlong;
typedef float    jfloat;
typedef double   jdouble;

#ifdef __cplusplus
class _jobject {};
class _jclass : public _jobject {};
class _jarray : public _jobject {};
class _jstring : public _jobject {};
class _jthrowable : public _jobject {};
class _jbooleanArray : public _jarray {};
class _jbyteArray : public _jarray {};
class _jcharArray : public _jarray {};
class _jshortArray : public _jarray {};
class _jintArray : public _jarray {};
class _jlongArray : public _jarray {};
class _jfloatArray : public _jarray {};
class _jdoubleArray : public _jarray {};
class _jobjectArray : public _jarray {};
typedef _jobject *jobject;
typedef _jclass *jclass;
typedef _jthrowable *jthrowable;
typedef _jstring *jstring;
typedef _jarray *jarray;
typedef _jbooleanArray *jbooleanArray;
typedef _jbyteArray *jbyteArray;
typedef _jcharArray *jcharArray;
typedef _jshortArray *jshortArray;
typedef _jintArray *jintArray;
typedef _jlongArray *jlongArray;
typedef _jfloatArray *jfloatArray;
typedef _jdoubleArray *jdoubleArray;
typedef _jobjectArray *jobjectArray;
#else
struct _jobject;
typedef struct _jobject *jobject;
typedef jobject jclass;
typedef jobject jthrowable;
typedef jobject jstring;
typedef jobject jarray;
typedef jarray jbooleanArray;
typedef jarray jbyteArray;
typedef jarray jcharArray;
typedef jarray jshortArray;
typedef jarray jintArray;
typedef jarray jlongArray;
typedef jarray jfloatArray;
typedef jarray jdoubleArray;
typedef jarray jobjectArray;
#endif

struct _jfieldID;
typedef struct _jfieldID *jfieldID;
struct _jmethodID;
typedef struct _jmethodID *jmethodID;

typedef jobject jweak;
typedef enum _jobjectType {
     JNIInvalidRefType    = 0,
     JNILocalRefType      = 1,
     JNIGlobalRefType     = 2,
     JNIWeakGlobalRefType = 3
} jobjectRefType;

typedef union jvalue {
    jboolean z;
    jbyte    b;
    jchar    c;
    jshort   s;
    jint     i;
    jlong    j;
    jfloat   f;
    jdouble  d;
    jobject  l;
} jvalue;

typedef struct {
    char *name;
    char *signature;
    void *fnPtr;
} JNINativeMethod;

struct JNINativeInterface_;
struct JNIEnv_;
struct JNIInvokeInterface_;
struct JavaVM_;

#ifdef __cplusplus
typedef JNIEnv_ JNIEnv;
typedef JavaVM_ JavaVM;
#else
typedef const struct JNINativeInterface_ *JNIEnv;
typedef const struct JNIInvokeInterface_ *JavaVM;
#endif

struct JNINativeInterface_ {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    void *reserved3;
    jint (JNICALL *GetVersion)(JNIEnv *env);
    jclass (JNICALL *DefineClass) (JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize len);
    jclass (JNICALL *FindClass) (JNIEnv *env, const char *name);
    jmethodID (JNICALL *FromReflectedMethod) (JNIEnv *env, jobject method);
    jfieldID (JNICALL *FromReflectedField) (JNIEnv *env, jobject field);
    jobject (JNICALL *ToReflectedMethod) (JNIEnv *env, jclass cls, jmethodID methodID, jboolean isStatic);
    jclass (JNICALL *GetSuperclass) (JNIEnv *env, jclass sub);
    jboolean (JNICALL *IsAssignableFrom) (JNIEnv *env, jclass sub, jclass sup);
    jobject (JNICALL *ToReflectedField) (JNIEnv *env, jclass cls, jfieldID fieldID, jboolean isStatic);
    jint (JNICALL *Throw) (JNIEnv *env, jthrowable obj);
    jint (JNICALL *ThrowNew) (JNIEnv *env, jclass clazz, const char *msg);
    jthrowable (JNICALL *ExceptionOccurred) (JNIEnv *env);
    void (JNICALL *ExceptionDescribe) (JNIEnv *env);
    void (JNICALL *ExceptionClear) (JNIEnv *env);
    void (JNICALL *FatalError) (JNIEnv *env, const char *msg);
    jint (JNICALL *PushLocalFrame) (JNIEnv *env, jint capacity);
    jobject (JNICALL *PopLocalFrame) (JNIEnv *env, jobject result);
    jobject (JNICALL *NewGlobalRef) (JNIEnv *env, jobject lobj);
    void (JNICALL *DeleteGlobalRef) (JNIEnv *env, jobject gref);
    void (JNICALL *DeleteLocalRef) (JNIEnv *env, jobject obj);
    jboolean (JNICALL *IsSameObject) (JNIEnv *env, jobject obj1, jobject obj2);
    jobject (JNICALL *NewLocalRef) (JNIEnv *env, jobject ref);
    jint (JNICALL *EnsureLocalCapacity) (JNIEnv *env, jint capacity);
    jobject (JNICALL *AllocObject) (JNIEnv *env, jclass clazz);
    jobject (JNICALL *NewObject) (JNIEnv *env, jclass clazz, jmethodID methodID, ...);
    jobject (JNICALL *NewObjectV) (JNIEnv *env, jclass clazz, jmethodID methodID, va_list args);
    jobject (JNICALL *NewObjectA) (JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
    jclass (JNICALL *GetObjectClass) (JNIEnv *env, jobject obj);
    jboolean (JNICALL *IsInstanceOf) (JNIEnv *env, jobject obj, jclass clazz);
    jmethodID (JNICALL *GetMethodID) (JNIEnv *env, jclass clazz, const char *name, const char *sig);
#define JNI_C_GEN_CALL(C_TYPE, JAVA_TYPE) \
    C_TYPE (JNICALL *Call##JAVA_TYPE##Method) (JNIEnv *env, jobject obj, jmethodID methodID, ...); \
    C_TYPE (JNICALL *Call##JAVA_TYPE##MethodV) (JNIEnv *env, jobject obj, jmethodID methodID, va_list args); \
    C_TYPE (JNICALL *Call##JAVA_TYPE##MethodA) (JNIEnv *env, jobject obj, jmethodID methodID, const jvalue * args);
    JNI_C_GEN_CALL(jobject, Object)
    JNI_C_GEN_CALL(jboolean, Boolean)
    JNI_C_GEN_CALL(jbyte, Byte)
    JNI_C_GEN_CALL(jchar, Char)
    JNI_C_GEN_CALL(jshort, Short)
    JNI_C_GEN_CALL(jint, Int)
    JNI_C_GEN_CALL(jlong, Long)
    JNI_C_GEN_CALL(jfloat, Float)
    JNI_C_GEN_CALL(jdouble, Double)
    JNI_C_GEN_CALL(void, Void)
#define JNI_C_GEN_NONVIRT_CALL(C_TYPE, JAVA_TYPE) \
    C_TYPE (JNICALL *CallNonvirtual##JAVA_TYPE##Method) (JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...); \
    C_TYPE (JNICALL *CallNonvirtual##JAVA_TYPE##MethodV) (JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args); \
    C_TYPE (JNICALL *CallNonvirtual##JAVA_TYPE##MethodA) (JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue * args);
    JNI_C_GEN_NONVIRT_CALL(jobject, Object)
    JNI_C_GEN_NONVIRT_CALL(jboolean, Boolean)
    JNI_C_GEN_NONVIRT_CALL(jbyte, Byte)
    JNI_C_GEN_NONVIRT_CALL(jchar, Char)
    JNI_C_GEN_NONVIRT_CALL(jshort, Short)
    JNI_C_GEN_NONVIRT_CALL(jint, Int)
    JNI_C_GEN_NONVIRT_CALL(jlong, Long)
    JNI_C_GEN_NONVIRT_CALL(jfloat, Float)
    JNI_C_GEN_NONVIRT_CALL(jdouble, Double)
    JNI_C_GEN_NONVIRT_CALL(void, Void)
    jfieldID (JNICALL *GetFieldID) (JNIEnv *env, jclass clazz, const char *name, const char *sig);
    jobject (JNICALL *GetObjectField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jboolean (JNICALL *GetBooleanField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jbyte (JNICALL *GetByteField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jchar (JNICALL *GetCharField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jshort (JNICALL *GetShortField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jint (JNICALL *GetIntField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jlong (JNICALL *GetLongField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jfloat (JNICALL *GetFloatField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    jdouble (JNICALL *GetDoubleField) (JNIEnv *env, jobject obj, jfieldID fieldID);
    void (JNICALL *SetObjectField) (JNIEnv *env, jobject obj, jfieldID fieldID, jobject val);
    void (JNICALL *SetBooleanField) (JNIEnv *env, jobject obj, jfieldID fieldID, jboolean val);
    void (JNICALL *SetByteField) (JNIEnv *env, jobject obj, jfieldID fieldID, jbyte val);
    void (JNICALL *SetCharField) (JNIEnv *env, jobject obj, jfieldID fieldID, jchar val);
    void (JNICALL *SetShortField) (JNIEnv *env, jobject obj, jfieldID fieldID, jshort val);
    void (JNICALL *SetIntField) (JNIEnv *env, jobject obj, jfieldID fieldID, jint val);
    void (JNICALL *SetLongField) (JNIEnv *env, jobject obj, jfieldID fieldID, jlong val);
    void (JNICALL *SetFloatField) (JNIEnv *env, jobject obj, jfieldID fieldID, jfloat val);
    void (JNICALL *SetDoubleField) (JNIEnv *env, jobject obj, jfieldID fieldID, jdouble val);
    jmethodID (JNICALL *GetStaticMethodID) (JNIEnv *env, jclass clazz, const char *name, const char *sig);
#define JNI_C_GEN_STATIC_CALL(C_TYPE, JAVA_TYPE) \
    C_TYPE (JNICALL *CallStatic##JAVA_TYPE##Method) (JNIEnv *env, jclass clazz, jmethodID methodID, ...); \
    C_TYPE (JNICALL *CallStatic##JAVA_TYPE##MethodV) (JNIEnv *env, jclass clazz, jmethodID methodID, va_list args); \
    C_TYPE (JNICALL *CallStatic##JAVA_TYPE##MethodA) (JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
    JNI_C_GEN_STATIC_CALL(jobject, Object)
    JNI_C_GEN_STATIC_CALL(jboolean, Boolean)
    JNI_C_GEN_STATIC_CALL(jbyte, Byte)
    JNI_C_GEN_STATIC_CALL(jchar, Char)
    JNI_C_GEN_STATIC_CALL(jshort, Short)
    JNI_C_GEN_STATIC_CALL(jint, Int)
    JNI_C_GEN_STATIC_CALL(jlong, Long)
    JNI_C_GEN_STATIC_CALL(jfloat, Float)
    JNI_C_GEN_STATIC_CALL(jdouble, Double)
    JNI_C_GEN_STATIC_CALL(void, Void)
    jfieldID (JNICALL *GetStaticFieldID) (JNIEnv *env, jclass clazz, const char *name, const char *sig);
    jobject (JNICALL *GetStaticObjectField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jboolean (JNICALL *GetStaticBooleanField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jbyte (JNICALL *GetStaticByteField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jchar (JNICALL *GetStaticCharField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jshort (JNICALL *GetStaticShortField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jint (JNICALL *GetStaticIntField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jlong (JNICALL *GetStaticLongField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jfloat (JNICALL *GetStaticFloatField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    jdouble (JNICALL *GetStaticDoubleField) (JNIEnv *env, jclass clazz, jfieldID fieldID);
    void (JNICALL *SetStaticObjectField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jobject value);
    void (JNICALL *SetStaticBooleanField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jboolean value);
    void (JNICALL *SetStaticByteField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jbyte value);
    void (JNICALL *SetStaticCharField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jchar value);
    void (JNICALL *SetStaticShortField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jshort value);
    void (JNICALL *SetStaticIntField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jint value);
    void (JNICALL *SetStaticLongField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jlong value);
    void (JNICALL *SetStaticFloatField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jfloat value);
    void (JNICALL *SetStaticDoubleField) (JNIEnv *env, jclass clazz, jfieldID fieldID, jdouble value);
    jstring (JNICALL *NewString) (JNIEnv *env, const jchar *unicode, jsize len);
    jsize (JNICALL *GetStringLength) (JNIEnv *env, jstring str);
    const jchar *(JNICALL *GetStringChars) (JNIEnv *env, jstring str, jboolean *isCopy);
    void (JNICALL *ReleaseStringChars) (JNIEnv *env, jstring str, const jchar *chars);
    jstring (JNICALL *NewStringUTF) (JNIEnv *env, const char *utf);
    jsize (JNICALL *GetStringUTFLength) (JNIEnv *env, jstring str);
    const char* (JNICALL *GetStringUTFChars) (JNIEnv *env, jstring str, jboolean *isCopy);
    void (JNICALL *ReleaseStringUTFChars) (JNIEnv *env, jstring str, const char* chars);
    jsize (JNICALL *GetArrayLength) (JNIEnv *env, jarray array);
    jobjectArray (JNICALL *NewObjectArray) (JNIEnv *env, jsize len, jclass clazz, jobject init);
    jobject (JNICALL *GetObjectArrayElement) (JNIEnv *env, jobjectArray array, jsize index);
    void (JNICALL *SetObjectArrayElement) (JNIEnv *env, jobjectArray array, jsize index, jobject val);
    jbooleanArray (JNICALL *NewBooleanArray) (JNIEnv *env, jsize len);
    jbyteArray (JNICALL *NewByteArray) (JNIEnv *env, jsize len);
    jcharArray (JNICALL *NewCharArray) (JNIEnv *env, jsize len);
    jshortArray (JNICALL *NewShortArray) (JNIEnv *env, jsize len);
    jintArray (JNICALL *NewIntArray) (JNIEnv *env, jsize len);
    jlongArray (JNICALL *NewLongArray) (JNIEnv *env, jsize len);
    jfloatArray (JNICALL *NewFloatArray) (JNIEnv *env, jsize len);
    jdoubleArray (JNICALL *NewDoubleArray) (JNIEnv *env, jsize len);
    jboolean * (JNICALL *GetBooleanArrayElements) (JNIEnv *env, jbooleanArray array, jboolean *isCopy);
    jbyte * (JNICALL *GetByteArrayElements) (JNIEnv *env, jbyteArray array, jboolean *isCopy);
    jchar * (JNICALL *GetCharArrayElements) (JNIEnv *env, jcharArray array, jboolean *isCopy);
    jshort * (JNICALL *GetShortArrayElements) (JNIEnv *env, jshortArray array, jboolean *isCopy);
    jint * (JNICALL *GetIntArrayElements) (JNIEnv *env, jintArray array, jboolean *isCopy);
    jlong * (JNICALL *GetLongArrayElements) (JNIEnv *env, jlongArray array, jboolean *isCopy);
    jfloat * (JNICALL *GetFloatArrayElements) (JNIEnv *env, jfloatArray array, jboolean *isCopy);
    jdouble * (JNICALL *GetDoubleArrayElements) (JNIEnv *env, jdoubleArray array, jboolean *isCopy);
    void (JNICALL *ReleaseBooleanArrayElements) (JNIEnv *env, jbooleanArray array, jboolean *elems, jint mode);
    void (JNICALL *ReleaseByteArrayElements) (JNIEnv *env, jbyteArray array, jbyte *elems, jint mode);
    void (JNICALL *ReleaseCharArrayElements) (JNIEnv *env, jcharArray array, jchar *elems, jint mode);
    void (JNICALL *ReleaseShortArrayElements) (JNIEnv *env, jshortArray array, jshort *elems, jint mode);
    void (JNICALL *ReleaseIntArrayElements) (JNIEnv *env, jintArray array, jint *elems, jint mode);
    void (JNICALL *ReleaseLongArrayElements) (JNIEnv *env, jlongArray array, jlong *elems, jint mode);
    void (JNICALL *ReleaseFloatArrayElements) (JNIEnv *env, jfloatArray array, jfloat *elems, jint mode);
    void (JNICALL *ReleaseDoubleArrayElements) (JNIEnv *env, jdoubleArray array, jdouble *elems, jint mode);
    void (JNICALL *GetBooleanArrayRegion) (JNIEnv *env, jbooleanArray array, jsize start, jsize l, jboolean *buf);
    void (JNICALL *GetByteArrayRegion) (JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf);
    void (JNICALL *GetCharArrayRegion) (JNIEnv *env, jcharArray array, jsize start, jsize len, jchar *buf);
    void (JNICALL *GetShortArrayRegion) (JNIEnv *env, jshortArray array, jsize start, jsize len, jshort *buf);
    void (JNICALL *GetIntArrayRegion) (JNIEnv *env, jintArray array, jsize start, jsize len, jint *buf);
    void (JNICALL *GetLongArrayRegion) (JNIEnv *env, jlongArray array, jsize start, jsize len, jlong *buf);
    void (JNICALL *GetFloatArrayRegion) (JNIEnv *env, jfloatArray array, jsize start, jsize len, jfloat *buf);
    void (JNICALL *GetDoubleArrayRegion) (JNIEnv *env, jdoubleArray array, jsize start, jsize len, jdouble *buf);
    void (JNICALL *SetBooleanArrayRegion) (JNIEnv *env, jbooleanArray array, jsize start, jsize l, const jboolean *buf);
    void (JNICALL *SetByteArrayRegion) (JNIEnv *env, jbyteArray array, jsize start, jsize len, const jbyte *buf);
    void (JNICALL *SetCharArrayRegion) (JNIEnv *env, jcharArray array, jsize start, jsize len, const jchar *buf);
    void (JNICALL *SetShortArrayRegion) (JNIEnv *env, jshortArray array, jsize start, jsize len, const jshort *buf);
    void (JNICALL *SetIntArrayRegion) (JNIEnv *env, jintArray array, jsize start, jsize len, const jint *buf);
    void (JNICALL *SetLongArrayRegion) (JNIEnv *env, jlongArray array, jsize start, jsize len, const jlong *buf);
    void (JNICALL *SetFloatArrayRegion) (JNIEnv *env, jfloatArray array, jsize start, jsize len, const jfloat *buf);
    void (JNICALL *SetDoubleArrayRegion) (JNIEnv *env, jdoubleArray array, jsize start, jsize len, const jdouble *buf);
    jint (JNICALL *RegisterNatives) (JNIEnv *env, jclass clazz, const JNINativeMethod *methods, jint nMethods);
    jint (JNICALL *UnregisterNatives) (JNIEnv *env, jclass clazz);
    jint (JNICALL *MonitorEnter) (JNIEnv *env, jobject obj);
    jint (JNICALL *MonitorExit) (JNIEnv *env, jobject obj);
    jint (JNICALL *GetJavaVM) (JNIEnv *env, JavaVM **vm);
    void (JNICALL *GetStringRegion) (JNIEnv *env, jstring str, jsize start, jsize len, jchar *buf);
    void (JNICALL *GetStringUTFRegion) (JNIEnv *env, jstring str, jsize start, jsize len, char *buf);
    void * (JNICALL *GetPrimitiveArrayCritical) (JNIEnv *env, jarray array, jboolean *isCopy);
    void (JNICALL *ReleasePrimitiveArrayCritical) (JNIEnv *env, jarray array, void *carray, jint mode);
    const jchar * (JNICALL *GetStringCritical) (JNIEnv *env, jstring string, jboolean *isCopy);
    void (JNICALL *ReleaseStringCritical) (JNIEnv *env, jstring string, const jchar *cstring);
    jweak (JNICALL *NewWeakGlobalRef) (JNIEnv *env, jobject obj);
    void (JNICALL *DeleteWeakGlobalRef) (JNIEnv *env, jweak ref);
    jboolean (JNICALL *ExceptionCheck) (JNIEnv *env);
    jobject (JNICALL *NewDirectByteBuffer) (JNIEnv* env, void* address, jlong capacity);
    void* (JNICALL *GetDirectBufferAddress) (JNIEnv* env, jobject buf);
    jlong (JNICALL *GetDirectBufferCapacity) (JNIEnv* env, jobject buf);
    jobjectRefType (JNICALL *GetObjectRefType) (JNIEnv* env, jobject obj);
    jobject (JNICALL *GetModule) (JNIEnv* env, jclass clazz);
    jboolean (JNICALL *IsVirtualThread) (JNIEnv* env, jobject obj);
};

struct JNIEnv_ {
    const struct JNINativeInterface_ *functions;
#ifdef __cplusplus
    jint GetVersion() { return functions->GetVersion(this); }
    jclass DefineClass(const char *name, jobject loader, const jbyte *buf, jsize len)
    { return functions->DefineClass(this, name, loader, buf, len); }
    jclass FindClass(const char *name)
    { return functions->FindClass(this, name); }
    jmethodID FromReflectedMethod(jobject method)
    { return functions->FromReflectedMethod(this, method); }
    jfieldID FromReflectedField(jobject field)
    { return functions->FromReflectedField(this,field); }
    jobject ToReflectedMethod(jclass cls, jmethodID methodID, jboolean isStatic)
    { return functions->ToReflectedMethod(this, cls, methodID, isStatic); }
    jclass GetSuperclass(jclass sub)
    { return functions->GetSuperclass(this, sub); }
    jboolean IsAssignableFrom(jclass sub, jclass sup)
    { return functions->IsAssignableFrom(this, sub, sup); }
    jobject ToReflectedField(jclass cls, jfieldID fieldID, jboolean isStatic)
    { return functions->ToReflectedField(this,cls,fieldID,isStatic); }
    jint Throw(jthrowable obj)
    { return functions->Throw(this, obj); }
    jint ThrowNew(jclass clazz, const char *msg)
    { return functions->ThrowNew(this, clazz, msg); }
    jthrowable ExceptionOccurred()
    { return functions->ExceptionOccurred(this); }
    void ExceptionDescribe()
    { functions->ExceptionDescribe(this); }
    void ExceptionClear()
    { functions->ExceptionClear(this); }
    void FatalError(const char *msg)
    { functions->FatalError(this, msg); }
    jint PushLocalFrame(jint capacity)
    { return functions->PushLocalFrame(this,capacity); }
    jobject PopLocalFrame(jobject result)
    { return functions->PopLocalFrame(this, result); }
    jobject NewGlobalRef(jobject lobj)
    { return functions->NewGlobalRef(this, lobj); }
    void DeleteGlobalRef(jobject gref)
    { functions->DeleteGlobalRef(this, gref); }
    void DeleteLocalRef(jobject obj)
    { functions->DeleteLocalRef(this, obj); }
    jboolean IsSameObject(jobject obj1, jobject obj2)
    { return functions->IsSameObject(this, obj1, obj2); }
    jobject NewLocalRef(jobject ref)
    { return functions->NewLocalRef(this, ref); }
    jint EnsureLocalCapacity(jint capacity)
    { return functions->EnsureLocalCapacity(this,capacity); }
    jobject AllocObject(jclass clazz)
    { return functions->AllocObject(this, clazz); }
    jobject NewObject(jclass clazz, jmethodID methodID, ...) {
        va_list args;
        va_start(args, methodID);
        jobject ret = functions->NewObjectV(this, clazz, methodID, args);
        va_end(args);
        return ret;
    }
    jobject NewObjectV(jclass clazz, jmethodID methodID, va_list args)
    { return functions->NewObjectV(this, clazz, methodID, args); }
    jobject NewObjectA(jclass clazz, jmethodID methodID, const jvalue *args)
    { return functions->NewObjectA(this, clazz, methodID, args); }
    jclass GetObjectClass(jobject obj)
    { return functions->GetObjectClass(this,obj); }
    jboolean IsInstanceOf(jobject obj, jclass clazz)
    { return functions->IsInstanceOf(this, obj, clazz); }
    jmethodID GetMethodID(jclass clazz, const char *name, const char *sig)
    { return functions->GetMethodID(this,clazz,name,sig); }
    void CallVoidMethod(jobject obj, jmethodID methodID, ...) {
        va_list args;
        va_start(args,methodID);
        functions->CallVoidMethodV(this,obj,methodID,args);
        va_end(args);
    }
    void CallVoidMethodV(jobject obj, jmethodID methodID, va_list args)
    { functions->CallVoidMethodV(this, obj, methodID, args); }
    void CallVoidMethodA(jobject obj, jmethodID methodID, const jvalue * args)
    { functions->CallVoidMethodA(this, obj, methodID, args); }
    void CallNonvirtualVoidMethod(jobject obj, jclass clazz, jmethodID methodID, ...) {
        va_list args;
        va_start(args,methodID);
        functions->CallNonvirtualVoidMethodV(this, obj, clazz, methodID, args);
        va_end(args);
    }
    void CallNonvirtualVoidMethodV(jobject obj, jclass clazz, jmethodID methodID, va_list args)
    { functions->CallNonvirtualVoidMethodV(this, obj, clazz, methodID, args); }
    void CallNonvirtualVoidMethodA(jobject obj, jclass clazz, jmethodID methodID, const jvalue * args)
    { functions->CallNonvirtualVoidMethodA(this, obj, clazz, methodID, args); }
    jfieldID GetFieldID(jclass clazz, const char *name, const char *sig)
    { return functions->GetFieldID(this, clazz, name, sig); }
    jmethodID GetStaticMethodID(jclass clazz, const char *name, const char *sig)
    { return functions->GetStaticMethodID(this,clazz,name,sig); }
    void CallStaticVoidMethod(jclass cls, jmethodID methodID, ...) {
        va_list args;
        va_start(args,methodID);
        functions->CallStaticVoidMethodV(this,cls,methodID,args);
        va_end(args);
    }
    void CallStaticVoidMethodV(jclass cls, jmethodID methodID, va_list args)
    { functions->CallStaticVoidMethodV(this, cls, methodID, args); }
    void CallStaticVoidMethodA(jclass cls, jmethodID methodID, const jvalue * args)
    { functions->CallStaticVoidMethodA(this, cls, methodID, args); }
    jfieldID GetStaticFieldID(jclass clazz, const char *name, const char *sig)
    { return functions->GetStaticFieldID(this, clazz, name, sig); }
    jstring NewString(const jchar *unicode, jsize len)
    { return functions->NewString(this, unicode, len); }
    jsize GetStringLength(jstring str)
    { return functions->GetStringLength(this,str); }
    const jchar *GetStringChars(jstring str, jboolean *isCopy)
    { return functions->GetStringChars(this, str, isCopy); }
    void ReleaseStringChars(jstring str, const jchar *chars)
    { functions->ReleaseStringChars(this, str, chars); }
    jstring NewStringUTF(const char *utf)
    { return functions->NewStringUTF(this, utf); }
    jsize GetStringUTFLength(jstring str)
    { return functions->GetStringUTFLength(this,str); }
    const char* GetStringUTFChars(jstring str, jboolean *isCopy)
    { return functions->GetStringUTFChars(this, str, isCopy); }
    void ReleaseStringUTFChars(jstring str, const char* chars)
    { functions->ReleaseStringUTFChars(this, str, chars); }
    jsize GetArrayLength(jarray array)
    { return functions->GetArrayLength(this, array); }
    jobjectArray NewObjectArray(jsize len, jclass clazz, jobject init)
    { return functions->NewObjectArray(this, len, clazz, init); }
    jobject GetObjectArrayElement(jobjectArray array, jsize index)
    { return functions->GetObjectArrayElement(this, array, index); }
    void SetObjectArrayElement(jobjectArray array, jsize index, jobject val)
    { functions->SetObjectArrayElement(this,array,index,val); }
    jint RegisterNatives(jclass clazz, const JNINativeMethod *methods, jint nMethods)
    { return functions->RegisterNatives(this, clazz, methods, nMethods); }
    jint UnregisterNatives(jclass clazz)
    { return functions->UnregisterNatives(this, clazz); }
    jint MonitorEnter(jobject obj)
    { return functions->MonitorEnter(this, obj); }
    jint MonitorExit(jobject obj)
    { return functions->MonitorExit(this, obj); }
    jint GetJavaVM(JavaVM **vm)
    { return functions->GetJavaVM(this, vm); }
    void GetStringRegion(jstring str, jsize start, jsize len, jchar *buf)
    { functions->GetStringRegion(this, str, start, len, buf); }
    void GetStringUTFRegion(jstring str, jsize start, jsize len, char *buf)
    { functions->GetStringUTFRegion(this, str, start, len, buf); }
    void * GetPrimitiveArrayCritical(jarray array, jboolean *isCopy)
    { return functions->GetPrimitiveArrayCritical(this, array, isCopy); }
    void ReleasePrimitiveArrayCritical(jarray array, void *carray, jint mode)
    { functions->ReleasePrimitiveArrayCritical(this, array, carray, mode); }
    const jchar * GetStringCritical(jstring string, jboolean *isCopy)
    { return functions->GetStringCritical(this, string, isCopy); }
    void ReleaseStringCritical(jstring string, const jchar *cstring)
    { functions->ReleaseStringCritical(this, string, cstring); }
    jweak NewWeakGlobalRef(jobject obj)
    { return functions->NewWeakGlobalRef(this, obj); }
    void DeleteWeakGlobalRef(jweak ref)
    { functions->DeleteWeakGlobalRef(this, ref); }
    jboolean ExceptionCheck()
    { return functions->ExceptionCheck(this); }
    jobject NewDirectByteBuffer(void* address, jlong capacity)
    { return functions->NewDirectByteBuffer(this, address, capacity); }
    void* GetDirectBufferAddress(jobject buf)
    { return functions->GetDirectBufferAddress(this, buf); }
    jlong GetDirectBufferCapacity(jobject buf)
    { return functions->GetDirectBufferCapacity(this, buf); }
    jobjectRefType GetObjectRefType(jobject obj)
    { return functions->GetObjectRefType(this, obj); }
    jobject GetModule(jclass clazz)
    { return functions->GetModule(this, clazz); }
    jboolean IsVirtualThread(jobject obj)
    { return functions->IsVirtualThread(this, obj); }
#define JNI_CPP_GEN_CALL(CPP_TYPE, JAVA_TYPE) \
    CPP_TYPE Call##JAVA_TYPE##Method(jobject obj, jmethodID methodID, ...) \
    { \
        va_list args; \
        va_start(args, methodID); \
        CPP_TYPE ret = functions->Call##JAVA_TYPE##MethodV(this, obj, methodID, args); \
        va_end(args); \
        return ret; \
    } \
    CPP_TYPE Call##JAVA_TYPE##MethodV(jobject obj, jmethodID methodID, va_list args) \
    { return functions->Call##JAVA_TYPE##MethodV(this, obj, methodID, args); } \
    CPP_TYPE Call##JAVA_TYPE##MethodA(jobject obj, jmethodID methodID, const jvalue* args) \
    { return functions->Call##JAVA_TYPE##MethodA(this, obj, methodID, args); }
#define JNI_CPP_GEN_NONVIRT_CALL(CPP_TYPE, JAVA_TYPE) \
    CPP_TYPE CallNonvirtual##JAVA_TYPE##Method(jobject obj, jclass clazz, jmethodID methodID, ...) \
    { \
        va_list args; \
        va_start(args, methodID); \
        CPP_TYPE ret = functions->CallNonvirtual##JAVA_TYPE##MethodV(this, obj, clazz, methodID, args); \
        va_end(args); \
        return ret; \
    } \
    CPP_TYPE CallNonvirtual##JAVA_TYPE##MethodV(jobject obj, jclass clazz, jmethodID methodID, va_list args) \
    { return functions->CallNonvirtual##JAVA_TYPE##MethodV(this, obj, clazz, methodID, args); } \
    CPP_TYPE CallNonvirtual##JAVA_TYPE##MethodA(jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) \
    { return functions->CallNonvirtual##JAVA_TYPE##MethodA(this, obj, clazz, methodID, args); }
#define JNI_CPP_GEN_GETFIELD(CPP_TYPE, JAVA_TYPE) \
    CPP_TYPE Get##JAVA_TYPE##Field(jobject obj, jfieldID fieldID) \
    { return functions->Get##JAVA_TYPE##Field(this, obj, fieldID); }
#define JNI_CPP_GEN_SETFIELD(CPP_TYPE, JAVA_TYPE) \
    void Set##JAVA_TYPE##Field(jobject obj, jfieldID fieldID, CPP_TYPE val) \
    { functions->Set##JAVA_TYPE##Field(this, obj, fieldID, val); }
#define JNI_CPP_GEN_STATIC_CALL(CPP_TYPE, JAVA_TYPE) \
    CPP_TYPE CallStatic##JAVA_TYPE##Method(jclass clazz, jmethodID methodID, ...) \
    { \
        va_list args; \
        va_start(args, methodID); \
        CPP_TYPE ret = functions->CallStatic##JAVA_TYPE##MethodV(this, clazz, methodID, args); \
        va_end(args); \
        return ret; \
    } \
    CPP_TYPE CallStatic##JAVA_TYPE##MethodV(jclass clazz, jmethodID methodID, va_list args) \
    { return functions->CallStatic##JAVA_TYPE##MethodV(this, clazz, methodID, args); } \
    CPP_TYPE CallStatic##JAVA_TYPE##MethodA(jclass clazz, jmethodID methodID, const jvalue* args) \
    { return functions->CallStatic##JAVA_TYPE##MethodA(this, clazz, methodID, args); }
#define JNI_CPP_GEN_GETSTATICFIELD(CPP_TYPE, JAVA_TYPE) \
    CPP_TYPE GetStatic##JAVA_TYPE##Field(jclass clazz, jfieldID fieldID) \
    { return functions->GetStatic##JAVA_TYPE##Field(this, clazz, fieldID); }
#define JNI_CPP_GEN_SETSTATICFIELD(CPP_TYPE, JAVA_TYPE) \
    void SetStatic##JAVA_TYPE##Field(jclass clazz, jfieldID fieldID, CPP_TYPE val) \
    { functions->SetStatic##JAVA_TYPE##Field(this, clazz, fieldID, val); }
#define JNI_CPP_GEN_NEWARRAY(CPP_ARR_TYPE, JAVA_TYPE) \
    CPP_ARR_TYPE New##JAVA_TYPE##Array(jsize len) \
    { return functions->New##JAVA_TYPE##Array(this, len); }
#define JNI_CPP_GEN_GETARRAYELEMENTS(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    CPP_TYPE* Get##JAVA_TYPE##ArrayElements(CPP_ARR_TYPE array, jboolean *isCopy) \
    { return functions->Get##JAVA_TYPE##ArrayElements(this, array, isCopy); }
#define JNI_CPP_GEN_RELEASEARRAYELEMENTS(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    void Release##JAVA_TYPE##ArrayElements(CPP_ARR_TYPE array, CPP_TYPE* elems, jint mode) \
    { functions->Release##JAVA_TYPE##ArrayElements(this, array, elems, mode); }
#define JNI_CPP_GEN_GETARRAYREGION(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    void Get##JAVA_TYPE##ArrayRegion(CPP_ARR_TYPE array, jsize start, jsize len, CPP_TYPE* buf) \
    { functions->Get##JAVA_TYPE##ArrayRegion(this, array, start, len, buf); }
#define JNI_CPP_GEN_SETARRAYREGION(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    void Set##JAVA_TYPE##ArrayRegion(CPP_ARR_TYPE array, jsize start, jsize len, const CPP_TYPE* buf) \
    { functions->Set##JAVA_TYPE##ArrayRegion(this, array, start, len, buf); }
#define JNI_CPP_GEN_CALLS_FIELDS(CPP_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_CALL(CPP_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_NONVIRT_CALL(CPP_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_GETFIELD(CPP_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_SETFIELD(CPP_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_STATIC_CALL(CPP_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_GETSTATICFIELD(CPP_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_SETSTATICFIELD(CPP_TYPE, JAVA_TYPE)
#define JNI_CPP_GEN_ARRAYS(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_NEWARRAY(CPP_ARR_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_GETARRAYELEMENTS(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_RELEASEARRAYELEMENTS(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_GETARRAYREGION(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE) \
    JNI_CPP_GEN_SETARRAYREGION(CPP_TYPE, CPP_ARR_TYPE, JAVA_TYPE)
    JNI_CPP_GEN_CALLS_FIELDS(jobject, Object)
    JNI_CPP_GEN_CALLS_FIELDS(jboolean, Boolean)
    JNI_CPP_GEN_CALLS_FIELDS(jbyte, Byte)
    JNI_CPP_GEN_CALLS_FIELDS(jchar, Char)
    JNI_CPP_GEN_CALLS_FIELDS(jshort, Short)
    JNI_CPP_GEN_CALLS_FIELDS(jint, Int)
    JNI_CPP_GEN_CALLS_FIELDS(jlong, Long)
    JNI_CPP_GEN_CALLS_FIELDS(jfloat, Float)
    JNI_CPP_GEN_CALLS_FIELDS(jdouble, Double)
    JNI_CPP_GEN_ARRAYS(jboolean, jbooleanArray, Boolean)
    JNI_CPP_GEN_ARRAYS(jbyte, jbyteArray, Byte)
    JNI_CPP_GEN_ARRAYS(jchar, jcharArray, Char)
    JNI_CPP_GEN_ARRAYS(jshort, jshortArray, Short)
    JNI_CPP_GEN_ARRAYS(jint, jintArray, Int)
    JNI_CPP_GEN_ARRAYS(jlong, jlongArray, Long)
    JNI_CPP_GEN_ARRAYS(jfloat, jfloatArray, Float)
    JNI_CPP_GEN_ARRAYS(jdouble, jdoubleArray, Double)
#endif /* __cplusplus */
};

typedef struct JavaVMOption {
    char *optionString;
    void *extraInfo;
} JavaVMOption;

typedef struct JavaVMInitArgs {
    jint version;
    jint nOptions;
    JavaVMOption *options;
    jboolean ignoreUnrecognized;
} JavaVMInitArgs;

typedef struct JavaVMAttachArgs {
    jint version;
    char *name;
    jobject group;
} JavaVMAttachArgs;

struct JNIInvokeInterface_ {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    jint (JNICALL *DestroyJavaVM)(JavaVM *vm);
    jint (JNICALL *AttachCurrentThread)(JavaVM *vm, void **penv, void *args);
    jint (JNICALL *DetachCurrentThread)(JavaVM *vm);
    jint (JNICALL *GetEnv)(JavaVM *vm, void **penv, jint version);
    jint (JNICALL *AttachCurrentThreadAsDaemon)(JavaVM *vm, void **penv, void *args);
};

struct JavaVM_ {
    const struct JNIInvokeInterface_ *functions;
#ifdef __cplusplus
    jint DestroyJavaVM()
    { return functions->DestroyJavaVM(this); }
    jint AttachCurrentThread(void **penv, void *args)
    { return functions->AttachCurrentThread(this, penv, args); }
    jint DetachCurrentThread()
    { return functions->DetachCurrentThread(this); }
    jint GetEnv(void **penv, jint version)
    { return functions->GetEnv(this, penv, version); }
    jint AttachCurrentThreadAsDaemon(void **penv, void *args)
    { return functions->AttachCurrentThreadAsDaemon(this, penv, args); }
#endif
};

JNIIMPORT jint JNICALL JNI_GetDefaultJavaVMInitArgs(void *args);
JNIIMPORT jint JNICALL JNI_CreateJavaVM(JavaVM **pvm, void **penv, void *args);
JNIIMPORT jint JNICALL JNI_GetCreatedJavaVMs(JavaVM **, jsize, jsize *);

/* Defined by native libraries. */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved);
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* !_JAVASOFT_JNI_H_ */
