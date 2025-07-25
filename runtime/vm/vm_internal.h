/*******************************************************************************
 * Copyright IBM Corp. and others 1991
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0-only WITH Classpath-exception-2.0 OR GPL-2.0-only WITH OpenJDK-assembly-exception-1.0
 *******************************************************************************/

#ifndef vm_internal_h
#define vm_internal_h

/* @ddr_namespace: map_to_type=VmInternalConstants */

/**
* @file vm_internal.h
* @brief Internal prototypes used within the VM module.
*
* This file contains implementation-private function prototypes and
* type definitions for the VM module.
*
*/

#include "j9.h"
#include "j9comp.h"
#include "vm_api.h"
#include "jni.h"
#include "vmi.h"

#ifdef __cplusplus
extern "C" {
#endif


/* ---------------- KeyHashTable.c ---------------- */
/* Tags for struct classTableEntry:
 *
 * Values stored in the table:
 *
 * xx000 RAM class
 * xxx01 ROM class
 * xxx11 Generated class
 *
 * Note - TAG_GENERATED_PACKAGE is only set with TAG_ROM_CLASS
 *
 * Values used only for query:
 *
 *  xx100 Query
 *  00100 Class UTF query
 *  01100 Packed query
 *  10100 Unicode query
 *  11100 Package UTF query
 */
#define MASK_RAM_CLASS			7
#define TAG_RAM_CLASS			0
#define	MASK_ROM_CLASS			3
#define	TAG_ROM_CLASS			1
#define MASK_PACKED				3
#define	TAG_PACKED_YES			3
#define TAG_GENERATED_PACKAGE		2
#define MASK_QUERY 				31
#define TAG_UTF_QUERY			4
#define TAG_PACKAGE_UTF_QUERY	28
#define TAG_PACKED_QUERY 		12
#define TAG_UNICODE_QUERY 		20

/* ---------------- resolvefield.c ---------------- */

/**
* @brief
* @param *vm
* @param *portLibrary
* @return J9HashTable *
*/
J9HashTable *
fieldIndexTableNew(J9JavaVM* vm, J9PortLibrary *portLib);


/**
* @brief
* @param *vm
* @return void
*/
void
fieldIndexTableFree(J9JavaVM* vm);

/* ---------------- jniinv.c ---------------- */

/**
* @brief
* @param p_vm
* @param p_env
* @param *vm_args
* @return jint
*/
jint JNICALL
JNI_CreateJavaVM(JavaVM ** p_vm, void ** p_env, void *vm_args);


/**
* @brief
* @param vm_buf
* @param bufLen
* @param nVMs
* @return jint
*/
jint JNICALL
JNI_GetCreatedJavaVMs(JavaVM ** vm_buf, jsize bufLen, jsize * nVMs);


/**
* @brief
* @param vm_args
* @return jint
*/
jint JNICALL
JNI_GetDefaultJavaVMInitArgs(void * vm_args);


#if (defined(J9VM_OPT_SIDECAR))
/**
* @brief
* @param shutdownThread
* @return void
*/
void
sidecarShutdown(J9VMThread* shutdownThread);
#endif /* J9VM_OPT_SIDECAR */

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
/**
* Helper function to invoke 31-bit target vm->exitHook via CEL4RO31.
* @param vm J9JavaVM instance
* @param rc The return code passed to exitHook
* @return void
*/
void
execute31BitExitHook(J9JavaVM * vm, jint rc);
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */

/* ---------------- jnimem.c ----------------- */

/**
* @brief
* @param vmThread
* @param sizeInBytes
* @return void*
*/
void*
jniArrayAllocateMemory32FromThread(J9VMThread* vmThread, UDATA sizeInBytes);

/**
* @brief
* @param vmThread
* @param location
* @return void
*/
void
jniArrayFreeMemory32FromThread(J9VMThread* vmThread, void* location);

/* ---------------- jnimisc.cpp -------------- */

#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
/**
* @brief
* @param env
* @param array
* @param isCopy
* @return void*
*/
void* JNICALL
getArrayElements31(JNIEnv *env, jarray array, jboolean *isCopy);

/**
* @brief
* @param env
* @param array
* @param elems
* @param mode
* @return void
*/
void JNICALL
releaseArrayElements31(JNIEnv *env, jarray array, void * elems, jint mode);

/**
* @brief
* @param env
* @param string
* @param isCopy
* @return const jchar*
*/
const jchar* JNICALL
getStringChars31(JNIEnv *env, jstring string, jboolean *isCopy);

/**
* @brief
* @param env
* @param string
* @param chars
* @return void
*/
void JNICALL
releaseStringChars31(JNIEnv *env, jstring string, const jchar * chars);

/**
* @brief
* @param env
* @param string
* @param isCopy
* @return const char*
*/
const char* JNICALL
getStringUTFChars31(JNIEnv *env, jstring string, jboolean *isCopy);

/**
* @brief
* @param env
* @param string
* @param chars
* @return void
*/
void JNICALL
releaseStringCharsUTF31(JNIEnv *env, jstring string, const char * chars);

/**
 * Helper function to query the matching 31-bit JNIEnv* for the given J9VMThread
 * parameter.  The 31-bit JNIEnv* is needed to pass as the JNIEnv* parameter into
 * cross-AMODE31 JNI natives.  Upon success, the 31-bit JNIEnv pointer will be
 * stored into vmThread->jniEnv31.
 *
 * @param[in]  vmThread The J9VMThread to query
 */
void
queryJNIEnv31(J9VMThread* vmThread);

/**
 * Helper function to query the matching 31-bit JavaVM* for the given J9JavaVM
 * parameter.  The 31-bit JavaVM* is needed to pass as the JavaVM* parameter into
 * cross-AMODE31 JNI natives.  Upon success, the 31-bit JavaVM pointer will be
 * stored into vm->javaVM31.
 *
 * @param[in]  vm The J9JavaVM to query
 */
void
queryJavaVM31(J9JavaVM* vm);
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */

/* ---------------- vmifunc.c ---------------- */
/**
* @brief
* @param vm
* @return vmiError
*/
vmiError
J9VMI_Initialize(J9JavaVM* vm);

/* ---------------- xcheck.c ---------------- */
/**
* @brief
* Modify the DLL load table for shared libraries used by -Xcheck: options.
* @param vm
* @param loadTable
* @param j9vm_args
* @return void
*/
jint
processXCheckOptions(J9JavaVM * vm, J9Pool* loadTable, J9VMInitArgs* j9vm_args);

/* ---------------- statistics.c ---------------- */
/**
* @brief
* @param *javaVM
* @return void
*/
void
deleteStatistics (J9JavaVM* javaVM);


/* ---------------- jnicsup.c ---------------- */
/**
* @brief
* parse -Xjni: options
* @param vm
* @param optArg
* @return IDATA
*/
IDATA
jniParseArguments(J9JavaVM *vm, char *optArg);


/**
 * Look up a JNI native in the specified \c nativeLibrary, if found then
 * fill in the \c nativeMethod->extra and \c nativeMethod->sendTarget fields.
 *
 * \param currentThread
 * \param nativeLibrary The library to scan for the C entrypoint.
 * \param nativeMethod The Java native method.
 * \param symbolName The name of the C entrypoint.
 * \param argSignature The signature needed by j9sl_lookup_name().
 * \return 0 on success, any other value on failure.
 */
UDATA
lookupJNINative(J9VMThread *currentThread, J9NativeLibrary *nativeLibrary, J9Method *nativeMethod, char * symbolName, char* argSignature);

/* ---------------- logsupport.c ---------------- */
/**
* @brief
* Process the -Xsyslog: options.
* @param vm
* @return JNI_ERR on error, JNI_OK on success.
*/
jint
processXLogOptions(J9JavaVM * vm);


/* ---------------- lockwordconfig.c ---------------- */
/**
 * This method should be called to clean up the lockword configuration
 *
 * @param jvm pointer to J9JavaVM that can be used by the method
 */
void
cleanupLockwordConfig(J9JavaVM* jvm);

/**
 * This method parses a string containing lockword options
 *
 * @param options string containing the options specified on the command line
 * @param what pointer to boolean set if the what option is specified
 *
 * @returns JNI_OK on success
 */
UDATA
parseLockwordConfig(J9JavaVM* jvm, char* options, BOOLEAN* what);

/**
 * This method is called to output the -Xlockword:what info
 *
 * @param jvm pointer to J9JavaVM that can be used by the method
 */
void
printLockwordWhat(J9JavaVM* jvm);



/* ------------------- stringhelpers.c ----------------- */
/**
 * Check that each UTF8 character is well-formed.
 * @param utf8Data pointer to a null-terminated sequence of bytes
 * @param length number of bytes in the string, not including the terminating '\0'
 * @return 1 if utf8Data points to valid UTF8, 0 otherwise
 */

UDATA
isValidUtf8(const U_8 * utf8Data, size_t length);

/**
 *  copies original to corrected, replacing bad characters with '?'.  Also add a terminating null byte.
 *
 * @param original pointer to a null-terminated sequence of bytes.
 * @param corrected pointer to a buffer to receive the corrected string.
 */

void
fixBadUtf8(const U_8 * original, U_8 *corrected, size_t length);

/* ------------------- NativeHelpers.cpp ----------------- */
/**
 * Create an array of java.lang.Class representing the list of
 * directly-implemented interfaces of a J9Class.
 *
 * A special stack frame must be on stack when calling this.
 *
 * @param currentThread[in] the current J9VMThread
 * @param clazz[in] the Class object to query (NullPointerException is thrown if this is NULL)
 *
 * @returns the array of interfaces, or NULL on failure (in which case an exception will be pending)
 */
j9object_t
getInterfacesHelper(J9VMThread *currentThread, j9object_t clazz);

/**
 * Iterate on stack to obtain the immediate caller class of the native method invoking
 * inlVMGetStackClassLoader(), inlVMGetStackClass() or newInstanceImpl().
 *
 * Check for @sun.reflect.CallerSensitive annotation & bootstrap/extension classloader at depth 1.
 * Skip Method.invoke(), MethodHandle.invokeWithArguments() and reflection classes at depth 0.
 *
 * @param currentThread[in] the current J9VMThread
 * @param walkState[in] the current J9StackWalkState pointer
 *
 * @returns J9_STACKWALK_KEEP_ITERATING with depth greater than 0, or J9_STACKWALK_STOP_ITERATING once the caller is detected at depth 0
 */
UDATA
cInterpGetStackClassJEP176Iterator(J9VMThread * currentThread, J9StackWalkState * walkState);

/**
 * Allocate native memory and copy the bytearray to it.
 *
 * @param currentThread[in] the current J9VMThread
 * @param byteArray[in] the ByteArray instance (must not be NULL)
 *
 * @returns the newly-allocated memory, or NULL on failure (no exception is set pending)
 */
char*
convertByteArrayToCString(J9VMThread *currentThread, j9object_t byteArray);

/**
 * Allocate a ByteArray and copy the C string into it, without the terminator.
 *
 * A special stack frame must be on stack when calling this.
 *
 * @param currentThread[in] the current J9VMThread
 * @param cString[in] the char * to copy from (must not be NULL)
 *
 * @returns the newly-allocated object, or NULL on failure (no exception is set pending)
 */
j9object_t
convertCStringToByteArray(J9VMThread *currentThread, const char *byteArray);

/**
 * Allocate native memory and copy the argument array to it.
 *
 * @param currentThread[in] the current J9VMThread
 * @param argArray[in] the specified argument array (must not be NULL)
 * @param javaArgs[in] the specified native memory to store the arguments (must not be NULL)
 *
 * @returns the newly-allocated memory, or NULL on failure (no exception is set pending)
 */
U_64 *
convertToNativeArgArray(J9VMThread *currentThread, j9object_t argArray, U_64 *javaArgs);

/* ------------------- romclasses.c ----------------- */

/**
 * Initialize the base type and array ROM classes.
 *
 * @param vm[in] the J9JavaVM
 */
void
initializeROMClasses(J9JavaVM *vm);

/* ------------------- visible.c ----------------- */

#if JAVA_SPEC_VERSION >= 11
/**
 * Check module access from srcModule to destModule.
 *
 * The algorithm for validating module access is:
 * 1) check to see if the source module `reads` the dest module.
 * 2) check to see if the dest module exports the package (that
 * dest class belongs to) to the source module.
 *
 * For reflective calls, the rules are slightly different as all reflect
 * reflect accesses implicitly have read access.  Set the
 *  J9_LOOK_REFLECT_CALL flag in the lookup options for reflective
 * checks.
 *
 * The unnamedModules export all the packages they own and have read access to all modules
 * they require access to.
 *
 * @param[in] currentThread the current J9VMThread
 * @param[in] vm the javaVM
 * @param[in] srcRomClass the accessing class
 * @param[in] srcModule the module of the src class
 * @param[in] destRomClass the accessing class
 * @param[in] destModule the module of the dest class
 * @param[in] destPackageID packageID of the dest class
 * @param[in] lookupOptions J9_LOOK* options
 *
 * @return 	J9_VISIBILITY_ALLOWED if the access is allowed,
 * 			J9_VISIBILITY_MODULE_READ_ACCESS_ERROR if module read access error occurred,
 * 			J9_VISIBILITY_MODULE_PACKAGE_EXPORT_ERROR if module package access error
 */

IDATA
checkModuleAccess(J9VMThread *currentThread, J9JavaVM* vm, J9ROMClass* srcRomClass, J9Module* srcModule, J9ROMClass* destRomClass, J9Module* destModule, UDATA destPackageID, UDATA lookupOptions);
#endif /* JAVA_SPEC_VERSION >= 11 */

/* ------------------- guardedstorage.c ----------------- */

#if defined(OMR_GC_CONCURRENT_SCAVENGER) && defined(J9VM_ARCH_S390)
/**
 * Hardware Read Barrier Handler
 *
 * The trap handler that gets invoked when a H/W Read Barrier is triggered
 */
J9_EXTERN_BUILDER_SYMBOL(handleHardwareReadBarrier);

/**
 * Handle a Guarded Storage Event
 *
 * A function that is used to notify the GC when a H/W Read Barrier is triggered
 *
 * @param currentThread[in] the current J9VMThread
 */
void
invokeJ9ReadBarrier(struct J9VMThread *currentThread);

/**
 * Initialize a thread to execute H/W Guarded Storage Instructions
 *
 * The vmThread parameter MUST be the current thread
 *
 * @param currentThread[in] the current J9VMThread
 *
 * @returns 1 if successful, 0 otherwise
 */
int32_t
j9gs_initializeThread(struct J9VMThread *vmThread);

/**
 * Deinitialize a thread to execute H/W Guarded Storage Instructions
 *
 * The vmThread parameter MUST be the current thread
 *
 * @param currentThread[in] the current J9VMThread
 *
 * @returns 1 if successful, 0 otherwise
 */
int32_t
j9gs_deinitializeThread(struct J9VMThread *vmThread);
#endif

/* FlushProcessWriteBuffers.cpp */

UDATA initializeExclusiveAccess(J9JavaVM *vm);
void shutDownExclusiveAccess(J9JavaVM *vm);

#if JAVA_SPEC_VERSION >= 16

/* ------------------- LayoutFFITypeHelpers.cpp ----------------- */

/**
 * Release the memory of struct specific ffi_types if exist in the argument/return types
 *
 * @param currentThread[in] the current J9VMThread
 * @param cifNode[in] the ffi_cif element in cifNativeCalloutDataCache
 */
void
freeAllStructFFITypes(J9VMThread *currentThread, void *cifNode);

/* ------------------- LayoutFFITypeTable.cpp ----------------- */

/**
 * @brief Create the layout string hashtable.
 *
 * @param vm a pointer to J9JavaVM
 * @return the hasthable
 */
J9HashTable *
createLayoutStrFFITypeTable(J9JavaVM *vm);

/**
 * @brief Release the layout string hashtable.
 *
 * @param hashtable a pointer to J9HashTable
 */
void
releaseLayoutStrFFITypeTable(J9HashTable *hashtable);

/**
 * @brief Search the layout string hashtable for the entry with the specified key.
 *
 * @param hashtable a pointer to J9HashTable
 * @param entry a pointer to J9LayoutStrFFITypeEntry
 * @return the requested entry if found; NULL otherwise
 */
J9LayoutStrFFITypeEntry *
findLayoutStrFFIType(J9HashTable *hashtable, J9LayoutStrFFITypeEntry *entry);

/**
 * @brief Add the entry with the populated key & value to the layout string hashtable.
 *
 * @param hashtable a pointer to J9HashTable
 * @param entry a pointer to J9LayoutStrFFITypeEntry
 * @return the same entry if done successfully; NULL otherwise
 */
J9LayoutStrFFITypeEntry *
addLayoutStrFFIType(J9HashTable *hashtable, J9LayoutStrFFITypeEntry *entry);

/* ------------------- UpcallThunkMem.cpp ----------------- */

/**
 * @brief Release the thunk heap list if exists.
 *
 * @param vm a pointer to J9JavaVM
 *
 * Note:
 * This function empties the thunk heap list by cleaning up all resources
 * created via allocateUpcallThunkMemory, including the generated thunk
 * and the corresponding metadata of each entry in the hashtable.
 */
void
releaseThunkHeap(J9JavaVM *vm);
#endif /* JAVA_SPEC_VERSION >= 16 */

/* ------------------- jnimisc.cpp ----------------- */

#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)

/**
 * Switch onto the zaap processor if not already running there.
 *
 * @param currentThread[in] the current J9VMThread
 * @param reason[in] the reason code
 */
void
javaOffloadSwitchOnWithReason(J9VMThread *currentThread, UDATA reason);

/**
 * Switch away from the zaap processor if running there.
 *
 * @param currentThread[in] the current J9VMThread
 * @param reason[in] the reason code
 */
void
javaOffloadSwitchOffWithReason(J9VMThread *currentThread, UDATA reason);

#define JAVA_OFFLOAD_SWITCH_ON_WITH_REASON_IF_LIMIT_EXCEEDED(currentThread, reason, length) \
	do { \
		if ((length) > J9_JNI_OFFLOAD_SWITCH_THRESHOLD) { \
			javaOffloadSwitchOnWithReason(currentThread, reason); \
		} \
	} while (0)

#define JAVA_OFFLOAD_SWITCH_OFF_WITH_REASON_IF_LIMIT_EXCEEDED(currentThread, reason, length) \
	do { \
		if ((length) > J9_JNI_OFFLOAD_SWITCH_THRESHOLD) { \
			javaOffloadSwitchOffWithReason(currentThread, reason); \
		} \
	} while (0)

#else /* defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT) */

#define JAVA_OFFLOAD_SWITCH_ON_WITH_REASON_IF_LIMIT_EXCEEDED(currentThread, reason, length)
#define JAVA_OFFLOAD_SWITCH_OFF_WITH_REASON_IF_LIMIT_EXCEEDED(currentThread, reason, length)

#endif /* defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT) */

#if defined(J9VM_OPT_JFR)

/* ------------------- jfr.cpp ------------------- */

/**
 * Begin event iteration in a JFR buffer.
 *
 * @param buffer[in] pointer to the buffer
 * @param walkState[in] pointer to the walk state
 *
 * @returns pointer to the first event in the buffer or NULL if the buffer is empty
 */
J9JFREvent*
jfrBufferStartDo(J9JFRBuffer *buffer, J9JFRBufferWalkState *walkState);

/**
 * Continue event iteration in a JFR buffer.
 *
 * @param walkState[in] pointer to the walk state
 *
 * @returns pointer to the next event in the buffer or NULL if there are no more
 */
J9JFREvent*
jfrBufferNextDo(J9JFRBufferWalkState *walkState);

#endif /* defined(J9VM_OPT_JFR) */

/* ------------------- ArrayCopyHelpers.cpp ----------------- */

/**
 * @brief A helper method which creates a copy of a heap array in native memory.
 *
 * @param currentThread the thread performing the copy
 * @param arrayObject the heap array to copy from
 * @param ensureMem32 a flag specific to 31-bit z/OS
 * @return the native array with the copied content
 */
void *
memcpyFromHeapArray(J9VMThread *currentThread, j9object_t arrayObject, jboolean ensureMem32);

/**
 * @brief A helper method which copies to a heap array from native memory,
 * then possibly releasing that native memory (to be paired with uses of
 * memcpyFromHeapArray).
 *
 * @param currentThread the thread performing the copy
 * @param arrayObject the heap array to copy into
 * @param elems the native memory to copy from
 * @param mode a code denoting whether to copy/release the native memory
 * @param ensureMem32 a flag specific to 31-bit z/OS
*/
void
memcpyToHeapArray(J9VMThread *currentThread, j9object_t arrayObject, void *elems, jint mode, jboolean ensureMem32);

/* ------------------- EnsureHashedConfig.cpp ----------------- */

/**
 * @brief This should be called to clean up the utf8String configuration.
 *
 * @param jvm pointer to J9JavaVM that can be used by the method
 */
void
cleanupEnsureHashedConfig(J9JavaVM *jvm);

/**
 * @brief This function parses a string containing utf8String options.
 *
 * @param options string containing the options specified on the command line
 * @returns JNI_OK on success
 */
UDATA
parseEnsureHashedConfig(J9JavaVM *jvm, char *options, BOOLEAN isAdd);

#if JAVA_SPEC_VERSION >= 11
/**
 * Get Module Name.
 *
 * *** The caller must free the memory from this pointer if the return value is NOT the buffer argument. ***
 *
 * @param[in] currentThread the current J9VMThread
 * @param[in] module the module
 * @param[in] buffer the buffer for the module name
 * @param[in] bufferLength the buffer length
 *
 * @return a char pointer to the module name
 */
char *
getModuleNameUTF(J9VMThread *currentThread, J9Module *module, char *buffer, UDATA bufferLength);
#endif /* JAVA_SPEC_VERSION >= 11 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* vm_internal_h */
