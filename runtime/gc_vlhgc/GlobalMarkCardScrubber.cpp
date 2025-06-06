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

#include "j9.h"
#include "j9cfg.h"
#include "j9port.h"
#include "modronopt.h"

#include "GlobalMarkCardScrubber.hpp"

#include "CardTable.hpp"
#include "ClassLoaderClassesIterator.hpp"
#include "ClassIterator.hpp"
#if JAVA_SPEC_VERSION >= 24
#include "ContinuationSlotIterator.hpp"
#endif /* JAVA_SPEC_VERSION >= 24 */
#include "CycleState.hpp"
#include "EnvironmentVLHGC.hpp"
#include "HeapMapWordIterator.hpp"
#include "HeapRegionDescriptorVLHGC.hpp"
#include "HeapRegionIterator.hpp"
#include "InterRegionRememberedSet.hpp"
#include "MarkMap.hpp"
#include "MixedObjectIterator.hpp"
#include "ParallelDispatcher.hpp"
#include "PointerArrayIterator.hpp"
#include "Task.hpp"
#include "WorkPacketsVLHGC.hpp"

#include "VMThreadStackSlotIterator.hpp"
#include "VMHelpers.hpp"

MM_GlobalMarkCardScrubber::MM_GlobalMarkCardScrubber(MM_EnvironmentVLHGC *env, MM_HeapMap *map, UDATA yieldCheckFrequency)
	: MM_CardCleaner()
	, _markMap(map)
	, _interRegionRememberedSet(MM_GCExtensions::getExtensions(env)->interRegionRememberedSet)
	, _yieldCheckFrequency(yieldCheckFrequency)
	, _countBeforeYieldCheck(yieldCheckFrequency)
{
	_statistics._dirtyCards = 0;
	_statistics._gmpMustScanCards = 0;
	_statistics._scrubbedCards = 0;
	_statistics._scrubbedObjects = 0;
}

void
MM_GlobalMarkCardScrubber::clean(MM_EnvironmentBase *envModron, void *lowAddress, void *highAddress, Card *cardToClean)
{
	MM_EnvironmentVLHGC* env = MM_EnvironmentVLHGC::getEnvironment(envModron);
	Assert_MM_true(MM_CycleState::CT_GLOBAL_MARK_PHASE == env->_cycleState->_collectionType);
	Assert_MM_true(env->_cycleState->_workPackets->isAllPacketsEmpty());

	if (!((MM_ParallelScrubCardTableTask *)env->_currentTask)->didTimeout()) {
		Card fromState = *cardToClean;
		Card toState = CARD_INVALID;
		switch(fromState) {
		case CARD_DIRTY:
			toState = CARD_PGC_MUST_SCAN;
			_statistics._dirtyCards += 1;
			break;
		case CARD_GMP_MUST_SCAN:
			toState = CARD_CLEAN;
			_statistics._gmpMustScanCards += 1;
			break;
		case CARD_CLEAN:
		case CARD_PGC_MUST_SCAN:
			/* other card states are not of interest to this cleaner and should be ignored */
			break;
		default:
			Assert_MM_unreachable();
		}
	
		if (CARD_INVALID != toState) {
			if (scrubObjectsInRange(env, lowAddress, highAddress)) {
				*cardToClean = toState;
			}
		}
	}
}

bool
MM_GlobalMarkCardScrubber::scrubObjectsInRange(MM_EnvironmentVLHGC *env, void *lowAddress, void *highAddress)
{
	bool doScrub = true;
	UDATA count = 0;
	
	/* we only support scanning exactly one card at a time */
	Assert_MM_true(0 == ((UDATA)lowAddress & (J9MODRON_HEAP_BYTES_PER_UDATA_OF_HEAP_MAP - 1)));
	Assert_MM_true(((UDATA)lowAddress + CARD_SIZE) == (UDATA)highAddress);
	
	for (UDATA bias = 0; bias < CARD_SIZE; bias += J9MODRON_HEAP_BYTES_PER_UDATA_OF_HEAP_MAP) {
		void *scanAddress = (void *)((UDATA)lowAddress + bias);
		MM_HeapMapWordIterator markedObjectIterator(_markMap, scanAddress);
		J9Object *fromObject = NULL;
		while (doScrub && (NULL != (fromObject = markedObjectIterator.nextObject()))) {
			doScrub = scrubObject(env, fromObject);
			count += 1;
		}
	}
	
	if (doScrub) {
		_statistics._scrubbedCards += 1;
		_statistics._scrubbedObjects += count;
	}
	
	return doScrub;
}

bool
MM_GlobalMarkCardScrubber::scrubObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr)
{
	bool doScrub = true;
	
	J9Class* clazz = J9GC_J9OBJECT_CLAZZ(objectPtr, env);
	Assert_MM_mustBeClass(clazz);
	switch(MM_GCExtensions::getExtensions(env)->objectModel.getScanType(clazz)) {
		case GC_ObjectModel::SCAN_ATOMIC_MARKABLE_REFERENCE_OBJECT:
		case GC_ObjectModel::SCAN_MIXED_OBJECT_LINKED:
		case GC_ObjectModel::SCAN_MIXED_OBJECT:
		case GC_ObjectModel::SCAN_OWNABLESYNCHRONIZER_OBJECT:
		case GC_ObjectModel::SCAN_REFERENCE_MIXED_OBJECT:
			doScrub = scrubMixedObject(env, objectPtr);
			break;
		case GC_ObjectModel::SCAN_CONTINUATION_OBJECT:
			doScrub = scrubContinuationObject(env, objectPtr);
			break;
		case GC_ObjectModel::SCAN_CLASS_OBJECT:
			doScrub = scrubClassObject(env, objectPtr);
			break;
		case GC_ObjectModel::SCAN_CLASSLOADER_OBJECT:
			doScrub = scrubClassLoaderObject(env, objectPtr);
			break;
		case GC_ObjectModel::SCAN_POINTER_ARRAY_OBJECT:
			doScrub = scrubPointerArrayObject(env, objectPtr);
			break;
		case GC_ObjectModel::SCAN_PRIMITIVE_ARRAY_OBJECT:
			doScrub = true;
			break;
		default:
			Trc_MM_GlobalMarkCardScrubber_scrubObject_invalid(env->getLanguageVMThread(), objectPtr);
			Assert_MM_unreachable();
	}
	
	return doScrub;
}

bool
MM_GlobalMarkCardScrubber::scrubMixedObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr)
{
	bool doScrub = true;

	/* No need to look at the class since it is immutable. However we can't assert that it's marked since the compactor may have deleted part of the mark map. */ 
	
	GC_MixedObjectIterator mixedObjectIterator(env->getOmrVM(), objectPtr);
	GC_SlotObject *slotObject = NULL;
	while (doScrub && (NULL != (slotObject = mixedObjectIterator.nextSlot()))) {
		J9Object* toObject = slotObject->readReferenceFromSlot();
		doScrub = mayScrubReference(env, objectPtr, toObject);
	}
	
	return doScrub;
}


void
stackSlotIteratorForGlobalMarkCardScrubber(J9JavaVM *javaVM, J9Object **slotPtr, void *localData, J9StackWalkState *walkState, const void *stackLocation)
{
	StackIteratorData4GlobalMarkCardScrubber *data = (StackIteratorData4GlobalMarkCardScrubber *)localData;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(javaVM);
	if (*data->doScrub && (extensions->heap->getHeapBase() <= *slotPtr) && (extensions->heap->getHeapTop() > *slotPtr)) {
		/* *slotPtr is heap object */
		*data->doScrub = data->globalMarkCardScrubber->mayScrubReference(data->env, data->fromObject, *slotPtr);
	}
	/* It's unfortunate, but we probably cannot terminate iteration of slots once we do see for one slot that we cannot scurb */
}

bool MM_GlobalMarkCardScrubber::scrubContinuationNativeSlots(MM_EnvironmentVLHGC *env, J9Object *objectPtr)
{
	bool doScrub = true;
	J9VMThread *currentThread = (J9VMThread *)env->getLanguageVMThread();
	const bool isConcurrentGC = false;
	const bool isGlobalGC = true;
	const bool beingMounted = false;
	if (MM_GCExtensions::needScanStacksForContinuationObject(currentThread, objectPtr, isConcurrentGC, isGlobalGC, beingMounted)) {
		StackIteratorData4GlobalMarkCardScrubber localData;
		localData.globalMarkCardScrubber = this;
		localData.env = env;
		localData.doScrub = &doScrub;
		localData.fromObject = objectPtr;

		GC_VMThreadStackSlotIterator::scanContinuationSlots(currentThread, objectPtr, (void *)&localData, stackSlotIteratorForGlobalMarkCardScrubber, false, false);

#if JAVA_SPEC_VERSION >= 24
		J9VMContinuation *continuation = J9VMJDKINTERNALVMCONTINUATION_VMREF(currentThread, objectPtr);
		GC_ContinuationSlotIterator continuationSlotIterator(currentThread, continuation);
		MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);

		J9Object **slotPtr = NULL;
		while (doScrub && (NULL != (slotPtr = continuationSlotIterator.nextSlot()))) {
			if ((extensions->heap->getHeapBase() <= *slotPtr) && (extensions->heap->getHeapTop() > *slotPtr)) {
				/* *slotPtr is heap object */
				doScrub = mayScrubReference(env, objectPtr, *slotPtr);
			}
		}
#endif /* JAVA_SPEC_VERSION >= 24 */

	}
	return doScrub;
}

bool MM_GlobalMarkCardScrubber::scrubContinuationObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr)
{
	bool doScrub = scrubMixedObject(env, objectPtr);
	if (doScrub) {
		doScrub = scrubContinuationNativeSlots(env, objectPtr);
	}
	return doScrub;
}

bool
MM_GlobalMarkCardScrubber::scrubPointerArrayObject(MM_EnvironmentVLHGC *env, J9Object *objectPtr)
{
	bool doScrub = true;

	/* No need to look at the class since it is immutable. However we can't assert that it's marked since the compactor may have deleted part of the mark map. */ 

	GC_PointerArrayIterator arrayIterator((J9JavaVM *)env->getLanguageVM(), objectPtr);
	GC_SlotObject *slotObject = NULL;
	while (doScrub && (NULL != (slotObject = arrayIterator.nextSlot()))) {
		J9Object* toObject = slotObject->readReferenceFromSlot();
		doScrub = mayScrubReference(env, objectPtr, toObject);
	}
	
	return doScrub;
}

bool 
MM_GlobalMarkCardScrubber::scrubClassObject(MM_EnvironmentVLHGC *env, J9Object *classObject)
{
	bool doScrub = scrubMixedObject(env, classObject);
	
	J9Class *classPtr = J9VM_J9CLASS_FROM_HEAPCLASS((J9VMThread*)env->getLanguageVMThread(), classObject);
	if (NULL != classPtr) {
		J9Object * volatile * slotPtr = NULL;
		/*
		 * Scan J9Class internals using general iterator
		 * - scan statics fields
		 * - scan call sites
		 * - scan MethodTypes
		 * - scan VarHandle MethodTypes
		 * - scan constants pool objects
		 */
		do {
			GC_ClassIterator classIterator(env, classPtr, false);
			while (doScrub && (NULL != (slotPtr = classIterator.nextSlot()))) {
				doScrub = mayScrubReference(env, classObject, *slotPtr);
			}

			classPtr = classPtr->replacedClass;
		} while (doScrub && (NULL != classPtr));
	}
	
	return doScrub;
}

bool 
MM_GlobalMarkCardScrubber::scrubClassLoaderObject(MM_EnvironmentVLHGC *env, J9Object *classLoaderObject)
{
	bool doScrub = scrubMixedObject(env, classLoaderObject);

	J9ClassLoader *classLoader = J9VMJAVALANGCLASSLOADER_VMREF((J9VMThread*)env->getLanguageVMThread(), classLoaderObject);
	if ((NULL != classLoader) && (0 == (classLoader->flags & J9CLASSLOADER_ANON_CLASS_LOADER))) {

		/* No lock is required because this only runs under exclusive access */
		/* (NULL == classLoader->classHashTable) is true ONLY for DEAD class loaders */
		Assert_MM_true(NULL != classLoader->classHashTable);
		GC_ClassLoaderClassesIterator iterator(MM_GCExtensions::getExtensions(env), classLoader);
		J9Class *clazz = NULL;
		while (doScrub && (NULL != (clazz = iterator.nextClass()))) {
			J9Object * classObject = J9VM_J9CLASS_TO_HEAPCLASS(clazz);
			Assert_MM_true(NULL != classObject);
			doScrub = mayScrubReference(env, classLoaderObject, classObject);
		}

		if (NULL != classLoader->moduleHashTable) {
			J9HashTableState walkState;
			J9JavaVM *javaVM = ((J9VMThread*)env->getLanguageVMThread())->javaVM;
			J9Module **modulePtr = (J9Module **)hashTableStartDo(classLoader->moduleHashTable, &walkState);
			while (doScrub && (NULL != modulePtr)) {
				J9Module * const module = *modulePtr;
				if (NULL != module->moduleObject) {
					doScrub = mayScrubReference(env, classLoaderObject, module->moduleObject);
				}
				if (doScrub) {
					doScrub = mayScrubReference(env, classLoaderObject, module->version);
				}
				modulePtr = (J9Module**)hashTableNextDo(&walkState);
			}

			if (classLoader == javaVM->systemClassLoader) {
				if (doScrub) {
					if (NULL != javaVM->unnamedModuleForSystemLoader->moduleObject) {
						doScrub = mayScrubReference(env, classLoaderObject, javaVM->unnamedModuleForSystemLoader->moduleObject);
					}
				}
			}
		}
	}
	
	return doScrub;
}

bool
MM_GlobalMarkCardScrubber::mayScrubReference(MM_EnvironmentVLHGC *env, J9Object *fromObject, J9Object* toObject)
{
	bool doScrub = true;

	if (_countBeforeYieldCheck == 0) {
		/* don't scrub the current card if we've run out of time, since we can't complete scanning this array */
		doScrub = !(env->_currentTask->shouldYieldFromTask(env));
		_countBeforeYieldCheck = _yieldCheckFrequency;
	} else {
		_countBeforeYieldCheck -= 1;
	}

	if (doScrub && (NULL != toObject)) {
		doScrub = (_markMap->isBitSet(toObject)) && !(_interRegionRememberedSet->shouldRememberReferenceForGlobalMark(env, fromObject, toObject));
	}
	return doScrub;
}

void
MM_ParallelScrubCardTableTask::run(MM_EnvironmentBase *envBase)
{
	MM_EnvironmentVLHGC *env = MM_EnvironmentVLHGC::getEnvironment(envBase);
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(env);
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	
	Trc_MM_ParallelScrubCardTableTask_scrubCardTable_Entry(env->getLanguageVMThread());
	Assert_MM_true(extensions->tarokEnableCardScrubbing);
	Assert_MM_true(MM_CycleState::CT_GLOBAL_MARK_PHASE == env->_cycleState->_collectionType);
	
	U_64 cleanStartTime = j9time_hires_clock();
	
	/* 4096 is an arbitrary number which determines how often the task checks if it should yield. 
	 * It currently matches MM_GlobalMarkingScheme::_arraySplitSize. 
	 */ 
	MM_GlobalMarkCardScrubber scrubber(env, env->_cycleState->_markMap, 4096);
	
	GC_HeapRegionIterator regionIterator(extensions->getHeap()->getHeapRegionManager());
	MM_HeapRegionDescriptor *region = NULL;
	while((!shouldYieldFromTask(envBase)) && (NULL != (region = regionIterator.nextRegion()))) {
		if (region->containsObjects()) {
			if(J9MODRON_HANDLE_NEXT_WORK_UNIT(env)) {
				if (env->_currentTask->shouldYieldFromTask(env)) {
					/* J9MODRON_HANDLE_NEXT_WORK_UNIT may be out of sync once we break. It must not be called again. */
				} else {
					extensions->cardTable->cleanCardsInRegion(env, &scrubber, region);
				}
			}
		}
	}
	
	U_64 cleanEndTime = j9time_hires_clock();
	env->_cardCleaningStats.addToCardCleaningTime(cleanStartTime, cleanEndTime);
	
	Trc_MM_ParallelScrubCardTableTask_scrubCardTable_Exit(env->getLanguageVMThread(),
		env->getWorkerID(),
		scrubber.getScrubbedObjects(), scrubber.getScrubbedCards(), scrubber.getDirtyCards(), scrubber.getGMPMustScanCards(),
		j9time_hires_delta(cleanStartTime, cleanEndTime, J9PORT_TIME_DELTA_IN_MICROSECONDS),
		didTimeout() ? "true" : "false");
}

void
MM_ParallelScrubCardTableTask::setup(MM_EnvironmentBase *env)
{
	if (!env->isMainThread()) {
		Assert_MM_true(NULL == env->_cycleState);
		env->_cycleState = _cycleState;
	} else {
		Assert_MM_true(_cycleState == env->_cycleState);
	}
}

void
MM_ParallelScrubCardTableTask::cleanup(MM_EnvironmentBase *env)
{
	if (!env->isMainThread()) {
		env->_cycleState = NULL;
	}
}

bool
MM_ParallelScrubCardTableTask::shouldYieldFromTask(MM_EnvironmentBase *env)
{
	if (!_timeLimitWasHit) {
		PORT_ACCESS_FROM_ENVIRONMENT(env);
		U_64 currentTime = j9time_hires_clock();
						
		if (currentTime >= _timeThreshold) {
			_timeLimitWasHit = true;
		}
	}
	return _timeLimitWasHit;
}

void
MM_ParallelScrubCardTableTask::synchronizeGCThreads(MM_EnvironmentBase *env, const char *id)
{
	/* this task doesn't use synchronization */
	Assert_MM_unreachable();
	MM_ParallelTask::synchronizeGCThreads(env, id);
}

bool
MM_ParallelScrubCardTableTask::synchronizeGCThreadsAndReleaseMain(MM_EnvironmentBase *env, const char *id)
{
	/* this task doesn't use synchronization */
	Assert_MM_unreachable();
	return MM_ParallelTask::synchronizeGCThreadsAndReleaseMain(env, id);
}

bool
MM_ParallelScrubCardTableTask::synchronizeGCThreadsAndReleaseSingleThread(MM_EnvironmentBase *env, const char *id)
{
	/* this task doesn't use synchronization */
	Assert_MM_unreachable();
	return MM_ParallelTask::synchronizeGCThreadsAndReleaseSingleThread(env, id);
}
