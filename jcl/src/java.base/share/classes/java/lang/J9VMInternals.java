/*[INCLUDE-IF JAVA_SPEC_VERSION >= 8]*/
/*
 * Copyright IBM Corp. and others 1998
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
 */
package java.lang;

import com.ibm.oti.vm.J9UnmodifiableClass;
import java.lang.ref.SoftReference;
import java.lang.reflect.*;
/*[IF JAVA_SPEC_VERSION < 24]*/
import java.security.AccessControlContext;
import java.security.AccessController;
import java.security.PrivilegedAction;
/*[ENDIF] JAVA_SPEC_VERSION < 24 */
import java.security.CodeSource;
import java.security.ProtectionDomain;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Properties;
import java.util.WeakHashMap;
import java.io.FileDescriptor;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintStream;
import java.net.URL;
import java.nio.charset.Charset;

/*[IF JAVA_SPEC_VERSION >= 9]*/
import jdk.internal.ref.CleanerShutdown;
import jdk.internal.ref.CleanerImpl;
import jdk.internal.misc.InnocuousThread;
import jdk.internal.misc.Unsafe;
/*[ELSE] JAVA_SPEC_VERSION >= 9 */
import sun.misc.Unsafe;
/*[ENDIF] JAVA_SPEC_VERSION >= 9 */
import com.ibm.oti.util.Msg;

@J9UnmodifiableClass
final class J9VMInternals {

	/*[REM] this field will be folded into methods compiled by the JIT and is needed for the fastIdentityHashCode optimization below */
	/*[REM] the real value of this field is set in the System.afterClinitInitialization since this class starts too early */
	/*[REM] do NOT set this field to null or javac can fold the null into fastIdentityHashCode and defeat the optimization */
	static final com.ibm.jit.JITHelpers jitHelpers = com.ibm.jit.JITHelpers.getHelpers();

	// cannot create any instances in <clinit> in this special class
	private static Map exceptions;

	static boolean initialized;
	private static Unsafe unsafe;

	/*[IF INLINE-TYPES]*/
	static boolean positiveOnlyHashcodes = positiveOnlyHashcodes();
	static Method valueObjectHashCode;
	/*[ENDIF] INLINE-TYPES */

	/* Ensure this class cannot be instantiated */
	private J9VMInternals() {
	}

	/*
	 * Called after everything else is initialized.
	 */
	static void threadCompleteInitialization() {
		/*[PR CMVC 99755] Implement -Djava.system.class.loader option */
		Thread.currentThread().internalSetContextClassLoader(ClassLoader.getSystemClassLoader());
		/*[IF JAVA_SPEC_VERSION > 8]*/
		jdk.internal.misc.VM.initLevel(4);
		/*[ELSE] JAVA_SPEC_VERSION > 8 */
		sun.misc.VM.booted();
		/*[ENDIF] JAVA_SPEC_VERSION > 8 */
		/*[IF Sidecar18-SE-OpenJ9 | (JAVA_SPEC_VERSION > 8)]*/
		System.startSNMPAgent();
		/*[ENDIF] Sidecar18-SE-OpenJ9 | (JAVA_SPEC_VERSION > 8) */

		/*[IF JAVA_SPEC_VERSION >= 11]*/
		System.finalizeConsoleEncoding();
		/*[ENDIF] JAVA_SPEC_VERSION >= 11 */
	}

	/*
	 * Called by the vm after everything else is initialized.
	 */
	private static void completeInitialization() {
		initialized = true;
		exceptions = new WeakHashMap();

		ClassLoader.completeInitialization();
		threadCompleteInitialization();

		/*[IF JAVA_SPEC_VERSION >= 9]*/
		System.initGPUAssist();

		if (Boolean.getBoolean("ibm.java9.forceCommonCleanerShutdown")) { //$NON-NLS-1$
			Runnable runnable = () -> {
				Thread[] threads;
				/*[IF JAVA_SPEC_VERSION < 19]*/
				ThreadGroup threadGroup;
				try {
					Field innocuousThreadGroupField = InnocuousThread.class.getDeclaredField("INNOCUOUSTHREADGROUP"); //$NON-NLS-1$
					innocuousThreadGroupField.setAccessible(true);
					threadGroup = (ThreadGroup)innocuousThreadGroupField.get(null);
				} catch (IllegalAccessException | NoSuchFieldException e) {
					throw new InternalError(e);
				}

				threads = new Thread[threadGroup.numThreads];
				threadGroup.enumerate(threads, false);
				/*[ELSE] JAVA_SPEC_VERSION < 19 */
				threads = Thread.getAllThreads();
				/*[ENDIF] JAVA_SPEC_VERSION < 19 */

				CleanerShutdown.shutdownCleaner();

				for (Thread t : threads) {
					if (t.getName().equals("Common-Cleaner")) { //$NON-NLS-1$
						t.interrupt();
						try {
							/* Need to wait for the Common-Cleaner thread to die before
							 * continuing. If not this will result in a race condition where
							 * the VM might attempt to shutdown before Common-Cleaner has a
							 * chance to stop properly. This will result in an unsuccessful
							 * shutdown and we will not release vm resources.
							 */
							t.join(3000);
							/* giving this a 3sec timeout. If it works it should work fairly
							 * quickly, 3 seconds should be more than enough time. If it doesn't
							 * work it may block indefinitely. Turning on -verbose:shutdown will
							 * let us know if it worked or not
							 */
						} catch (Throwable e) {
							/* empty block */
						}
						break;
					}
				}
			};
			Runtime.getRuntime().addShutdownHook(new Thread(runnable, "CommonCleanerShutdown", true, false, false, null)); //$NON-NLS-1$
		}
		/*[ENDIF] JAVA_SPEC_VERSION >= 9 */
		/*[IF CRAC_SUPPORT]*/
		if (openj9.internal.criu.InternalCRIUSupport.isCRaCSupportEnabled()) {
			// export java.base/jdk.crac unconditionally
			J9VMInternals.class.getModule().implAddExports("jdk.crac"); //$NON-NLS-1$

			// export jdk.management/jdk.crac.management unconditionally
			java.util.Optional<Module> om = ModuleLayer.boot().findModule("jdk.management"); //$NON-NLS-1$
			if (om.isPresent()) {
				om.get().implAddExports("jdk.crac.management"); //$NON-NLS-1$
			}
		}
		/*[ENDIF] CRAC_SUPPORT */
		/*[IF (11 <= JAVA_SPEC_VERSION) & (JAVA_SPEC_VERSION <= 17)]*/
		/* ImageReader should be initialized before main() is called to
		 * avoid being affected by a potential invalid java.home path.
		 */
		try {
			jdk.internal.jimage.ImageReaderFactory.getImageReader();
		} catch (java.io.UncheckedIOException e) {
			// Ignored deliberately.
		}
		/*[ENDIF] (11 <= JAVA_SPEC_VERSION) & (JAVA_SPEC_VERSION <= 17) */
	}

	/**
	 * Initialize a Class. See chapter
	 * 2.17.5 of the JVM Specification (2nd ed)
	 */
	static void initialize(Class<?> clazz) {
		/* initializationLock == null means initialization successfully completed */
		if (null != clazz.initializationLock) {
			Unsafe localUnsafe = unsafe;
			if (null == localUnsafe) {
				localUnsafe = unsafe = Unsafe.getUnsafe();
			}
			localUnsafe.ensureClassInitialized(clazz);
		}
	}

	/**
	 * Throw a descriptive NoClassDefFoundError if an attempt is made
	 * to initialize a Class which has previously failed initialization.
	 */
	private static void initializationAlreadyFailed(Class clazz) {
		/*[PR 118650] CLDC 1.1 includes java.lang.NoClassDefFoundError */
		/*[PR 118653, CMVC 111503] Throw better NoClassDefFoundError for failed Class initialization */
		NoClassDefFoundError notFound = new NoClassDefFoundError(clazz.getName() + " (initialization failure)"); //$NON-NLS-1$
		// if exceptions is null, we're initializing and running single threaded
		if (exceptions != null) {
			synchronized(exceptions) {
				/*[PR CMVC 195512] Throwable for class init error retained using WeakReference */
				SoftReference weakReason = (SoftReference)exceptions.get(clazz);
				if (weakReason != null) {
					Throwable reason = (Throwable)weakReason.get();
					if (reason != null) {
						reason = copyThrowable(reason);
						notFound.initCause(reason);
					}
				}
			}
		}
		throw notFound;
	}

	/**
	 * Record the cause (Throwable) of a failed initialization for a Class.
	 * If the Throwable is an Error, throw that, otherwise, wrap the Throwable
	 * in an ExceptionInInitializerError and throw that instead.
	 */
	private static void recordInitializationFailure(Class clazz, Throwable err) {
		if (initialized) {
			// if exceptions is null, we're initializing and running single threaded
			if (exceptions == null)
				exceptions = new WeakHashMap();
			synchronized(exceptions) {
/*[IF JAVA_SPEC_VERSION >= 18]*/
				if (!(err instanceof Error)) {
					err = new ExceptionInInitializerError(err);
				}
				exceptions.put(clazz, new SoftReference(copyThrowable(err)));
/*[ELSE] JAVA_SPEC_VERSION >= 18*/
				Throwable cause = err;
				if (err instanceof ExceptionInInitializerError) {
					cause = ((ExceptionInInitializerError)err).getException();
					if (cause == null) {
						/* Use the original ExceptionInInitializerError */
						cause = err;
					}
				}
				exceptions.put(clazz, new SoftReference(copyThrowable(cause)));
/*[ENDIF] JAVA_SPEC_VERSION >= 18*/
			}
		}
		ensureError(err);
	}

	/**
	 * If the incoming Throwable is an instance of Error, throw it.
	 * Otherwise, wrap it in a new ExceptionInInitializerError and throw that.
	 */
	private static void ensureError(Throwable err) {
		if (err instanceof Error) {
			throw (Error)err;
		}
		throw new ExceptionInInitializerError(err);
	}

	private static native Throwable newInstance(Class exceptionClass, Class constructorClass);

	private static Throwable cloneThrowable(final Throwable throwable, final HashMap hashMapThrowable) {
		/*[IF JAVA_SPEC_VERSION < 24]*/
		return (Throwable)AccessController.doPrivileged(new PrivilegedAction() {
			@Override
			public Object run() {
		/*[ENDIF] JAVA_SPEC_VERSION < 24 */
				Throwable clone;
				try {
					Class cls = throwable.getClass();
					clone = newInstance(cls, Object.class);
					while (cls != null) {
						Field[] fields = cls.getDeclaredFields();
						for (int i=0; i<fields.length; i++) {
							if (!Modifier.isStatic(fields[i].getModifiers()) &&
									!(cls == Throwable.class && fields[i].getName().equals("walkback"))) //$NON-NLS-1$
							{
								fields[i].setAccessible(true);
								Object value;
								if (cls == Throwable.class && fields[i].getName().equals("cause")) { //$NON-NLS-1$
									value = clone;
								} else {
									value = fields[i].get(throwable);
									//	Only copy throwable fields whose stacktrace might be kept within Map exceptions
									//	The throwable stored within Map exceptions as WeakReference could be retrieved (before being GCed) later
									if (value instanceof Throwable) {
										value = copyThrowable((Throwable)value, hashMapThrowable);
									}
								}
								fields[i].set(clone, value);
							}
						}
						cls = getSuperclass(cls);
					}
				} catch (Throwable e) {
					/*[MSG "K05c3", "Error cloning Throwable ({0}). The original exception was: {1}"]*/
					clone = new Throwable(Msg.getString("K05c3", e, throwable.toString())); //$NON-NLS-1$
				}
				return clone;
		/*[IF JAVA_SPEC_VERSION < 24]*/
			}
		});
		/*[ENDIF] JAVA_SPEC_VERSION < 24 */
	}

	/**
	 * Entry method to copy the specified Throwable, invoke copyThrowable(Throwable, HashMap)
	 * to check loop such that we don't go infinite.
	 *
	 * @param throwable the Throwable to copy
	 *
	 * @return a copy of the Throwable
	 */
	private static Throwable copyThrowable(Throwable throwable) {
		HashMap hashMapThrowable = new HashMap();
		/*[PR CMVC 199629] Exception During Class Initialization Not Handled Correctly */
		return copyThrowable(throwable, hashMapThrowable);
	}

	/**
	 * Copy the specified Throwable, wrapping the stack trace for each
	 * Throwable. Check for loops so we don't go infinite.
	 *
	 * @param throwable the Throwable to copy
	 * @param hashMapThrowable the Throwables already cloned
	 *
	 * @return a copy of the Throwable or itself if it has been cloned already
	 */
	private static Throwable copyThrowable(Throwable throwable, HashMap hashMapThrowable) {
		if (hashMapThrowable.get(throwable) != null) {
			//	stop recursive call here when the throwable has been cloned
			return	throwable;
		}
		hashMapThrowable.put(throwable, throwable);
		Throwable root = cloneThrowable(throwable, hashMapThrowable);
		root.setStackTrace(throwable.getStackTrace());
		Throwable parent = root;
		Throwable cause = throwable.getCause();
		//	looking for causes recursively which will be part of stacktrace	stored into Map exceptions
		while (cause != null && hashMapThrowable.get(cause) == null) {
			hashMapThrowable.put(cause, cause);
			Throwable child = cloneThrowable(cause, hashMapThrowable);
			child.setStackTrace(cause.getStackTrace());
			parent.setCause(child);
			parent = child;
			cause = cause.getCause();
		}
		return root;
	}

	/**
/*[IF JAVA_SPEC_VERSION < 20]
	 * Private method to be called by the VM after a Threads dies and throws ThreadDeath.
/*[ELSE]
	 * Private method to be called by the VM after a Threads dies.
/*[ENDIF]
	 * It has to <code>notifyAll()</code> so that <code>join</code> can work properly.
	 * However, it has to be done when the Thread is "thought of" as being dead by other
	 * observer Threads (<code>isAlive()</code> has to return false for the Thread
	 * running this method or <code>join</code> may never return)
	 *
	 * @author		OTI
	 * @version		initial
	 */
	private static void threadCleanup(Thread thread) {
		// don't synchronize the remove! Otherwise deadlock may occur
		/*[PR 106323] -- remove might throw an exception, so make sure we finish the cleanup*/
		try {
			// Leave the ThreadGroup. This is why remove can't be private
			/*[IF JAVA_SPEC_VERSION < 19]*/
			thread.group.remove(thread);
			/*[ENDIF] JAVA_SPEC_VERSION < 19 */
		} finally {
			thread.exit();

			synchronized (thread) {
				thread.notifyAll();
			}
		}
	}

	/*[IF JAVA_SPEC_VERSION < 24]*/
	/*[PR CVMC 124584] checkPackageAccess(), not defineClassImpl(), should use ProtectionDomain */
	private static void checkPackageAccess(final Class<?> clazz, ProtectionDomain pd) {
		@SuppressWarnings("removal")
		final SecurityManager sm = System.getSecurityManager();
		if (sm != null) {
			ProtectionDomain[] pdArray = (pd == null) ? new ProtectionDomain[] {} : new ProtectionDomain[] { pd };
			AccessController.doPrivileged(new PrivilegedAction<Object>() {
				@Override
				public Object run() {
					String packageName = clazz.getPackageName();
					if (packageName != null) {
						sm.checkPackageAccess(packageName);
					}
					if (Proxy.isProxyClass(clazz)) {
						/*[PR CMVC 198986] Fix proxies */
						ClassLoader cl = clazz.getClassLoaderImpl();
						sun.reflect.misc.ReflectUtil.checkProxyPackageAccess(cl, clazz.getInterfaces());
					}
					return null;
				}
			}, new AccessControlContext(pdArray));
		}
	}
	/*[ENDIF] JAVA_SPEC_VERSION < 24 */

	/*[PR CMVC 104341] Exceptions in Object.finalize() not ignored */

	private static void runFinalize(Object obj) {
		try {
			obj.finalize();
		} catch(Throwable e) {}
	}

	static native StackTraceElement[] getStackTrace(Throwable throwable, boolean pruneConstructors);

	private static native void prepareClassImpl(Class clazz);

	/**
	 * Prepare the specified Class. Fill in initial field values, and send
	 * the class prepare event.
	 *
	 * @param clazz the Class to prepare
	 */
	static void prepare(Class clazz) {
		if (clazz.initializationLock == null) {
			/* initializationLock == null means initialization successfully completed */
			return;
		}
		prepareClassImpl(clazz);
	}

	/**
	 * Determines the superclass of specified <code>clazz</code>.
	 * @param clazz The class to introspect (must not be null).
	 * @return The superclass, or null for primitive types and interfaces.
	 */
	static native Class getSuperclass(Class clazz);

	/**
	 * Determines the interfaces implemented by <code>clazz</code>.
	 * @param clazz The class to introspect (must not be null).
	 * @return An array of all interfaces supported by <code>clazz</code>.
	 */
	static native Class[] getInterfaces(Class clazz);

	/**
	 * Answers a new instance of the class represented by the
	 * <code>clazz</code>, created by invoking the default (i.e. zero-argument)
	 * constructor. If there is no such constructor, or if the
	 * creation fails (either because of a lack of available memory or
	 * because an exception is thrown by the constructor), an
	 * InstantiationException is thrown. If the default constructor
	 * exists, but is not accessible from the context where this
	 * message is sent, an IllegalAccessException is thrown.
	 *
	 * @param clazz The class to create an instance of.
	 * @return		a new instance of the class represented by the receiver.
	 * @throws		IllegalAccessException if the constructor is not visible to the sender.
	 * @throws		InstantiationException if the instance could not be created.
	 */
	native static Object newInstanceImpl(Class clazz)
		throws IllegalAccessException, InstantiationException;

	/**
	 * Answers an integer hash code for the parameter.
	 * The hash code returned is the same one that would
	 * be returned by java.lang.Object.hashCode(), whether
	 * or not the object's class has overridden hashCode().
	 * Calling with null will cause a crash, so the caller
	 * must check for null. This version looks for a cached
	 * hash value using Java code to avoid calls to native
	 * code whenever possible and should be preferred to
	 * identityHashCode unless you know you don't want this
	 * optimization.
	 *
	 * @param		anObject	the object
	 * @return		the hash code for the object
	 *
	 * @see			java.lang.Object#hashCode
	 */
	static int fastIdentityHashCode(Object anObject) {
		com.ibm.jit.JITHelpers h = jitHelpers;
		if (null == h) {
			return identityHashCode(anObject); /* use early returns to make the JIT code faster */
		}
		if (h.isArray(anObject)) {
			return identityHashCode(anObject); /* use early returns to make the JIT code faster */
		}
		if (h.is32Bit()) {
			int ptr = h.getIntFromObject(anObject, 0L);
			if ((ptr & com.ibm.oti.vm.VM.OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS) != 0) {
				int j9class = ptr & com.ibm.oti.vm.VM.J9_JAVA_CLASS_MASK;
				return h.getIntFromObject(anObject, h.getBackfillOffsetFromJ9Class32(j9class));
			}
		} else {
			long ptr = (com.ibm.oti.vm.VM.FJ9OBJECT_SIZE == 4) ? Integer.toUnsignedLong(h.getIntFromObject(anObject, 0L)) : h.getLongFromObject(anObject, 0L);
			if ((ptr & com.ibm.oti.vm.VM.OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS) != 0) {
				long j9class = ptr & com.ibm.oti.vm.VM.J9_JAVA_CLASS_MASK;
				return h.getIntFromObject(anObject, h.getBackfillOffsetFromJ9Class64(j9class));
			}
		}
		return identityHashCode(anObject);
	}

	/**
	 * Answers an integer hash code for the parameter.
	 * The hash code returned is the same one that would
	 * be returned by java.lang.Object.hashCode(), whether
	 * or not the object's class has overridden hashCode().
	 * Calling with null will cause a crash, so the caller
	 * must check for null.
	 *
	 * @param		anObject	the object
	 * @return		the hash code for the object
	 *
	 * @see			java.lang.Object#hashCode
	 */
	static native int identityHashCode(Object anObject);

	/*[IF INLINE-TYPES]*/
	/**
	 * Answers an integer hash code for the parameter.
	 * The caller must ensure that the parameter is a value type.
	 * The hash code returned is the same one that would
	 * be returned by java.lang.Object.hashCode(), assuming
	 * the object's class has not overridden hashCode() and
	 * that the parameter is a value type.
	 *
	 * @param		anObject	the object
	 * @return		the hash code for the object
	 * @throws		InternalError if the object is not a value type
	 *
	 * @see			java.lang.Object#hashCode
	 */
	static int valueHashCode(Object anObject) {
		int hashcode;

		if (null == valueObjectHashCode) {
			try {
				Class<?> valueObjectMethods = Class.forName("java.lang.runtime.ValueObjectMethods");
				valueObjectHashCode = valueObjectMethods.getDeclaredMethod("valueObjectHashCode", Object.class);
				valueObjectHashCode.setAccessible(true);
			} catch (ClassNotFoundException | NoSuchMethodException e) {
				throw new InternalError(e);
			}
		}

		try {
			hashcode = (Integer)valueObjectHashCode.invoke(null, anObject);
		} catch (IllegalAccessException | InvocationTargetException e) {
			throw new InternalError(e);
		}

		if (positiveOnlyHashcodes) {
			hashcode &= 0x7FFF_FFFF;
		}

		return hashcode;
	}

	/**
	 * Returns true if hash codes are positive only,
	 * false otherwise.
	 *
	 * @return		A boolean indicating whether hash codes are all positive
	 */
	static native boolean positiveOnlyHashcodes();
	/*[ENDIF] INLINE-TYPES */

	/**
	 * Primitive implementation of Object.clone().
	 * Calling with null will cause a crash, so the caller
	 * must check for null.
	 *
	 * @param		anObject	the object
	 * @return		a shallow copy of anObject
	 *
	 * @see			java.lang.Object#clone
	 */
	static native Object primitiveClone(Object anObject) throws CloneNotSupportedException;

	/**
	 * A class used for synchronizing Class initialization.
	 *
	 * The instance field is currently only for debugging purposes.
	 */
	static final class ClassInitializationLock {
		Class theClass;
	}

	/**
	 * Native used to dump a string to the system console for debugging.
	 *
	 * @param 		str String
	 *					the String to display
	 */
	public static native void dumpString(String str);

	private static String[] getClassInfoStrings(final Class<?> clazz, String classPath){
		String classLoaderStr = "<Bootstrap Loader>"; //$NON-NLS-1$
		String cpResult = "<Unknown>"; //$NON-NLS-1$

		if (classPath == null) {
			ClassLoader classLoader = clazz.getClassLoader();
			if (classLoader != null) {
				classLoaderStr = classLoader.toString();
				/*[IF JAVA_SPEC_VERSION < 24]*/
				classPath = AccessController.doPrivileged(new PrivilegedAction<String>() {
					@Override
					public String run() {
				/*[ENDIF] JAVA_SPEC_VERSION < 24 */
						String path = null;
						try {
							ProtectionDomain pd = clazz.getProtectionDomain();
							if (pd != null) {
								CodeSource cs = pd.getCodeSource();
								if (cs != null) {
									URL loc = cs.getLocation();
									if (loc != null) {
										path = loc.toString();
									}
								}
							}
						} catch (Exception e) {
						}
				/*[IF JAVA_SPEC_VERSION >= 24]*/
						classPath = path;
				/*[ELSE] JAVA_SPEC_VERSION >= 24 */
						return path;
					}
				});
				/*[ENDIF] JAVA_SPEC_VERSION >= 24 */
			}
		}
		if (classPath != null) {
			cpResult = classPath;
		}
		String [] strings = {cpResult, classLoaderStr};
		return strings;
	}

	/**
	 * Format a message to be used when creating a NoSuchMethodException from the VM.
	 * On failure returns methodSig ie the old style of NoSuchMethodException message
	 * @param methodSig String representation of the signature of the called method
	 * @param clazz1 The calling class,
	 * @param classPath1 Classpath used to load calling class. Only set when class is loaded by bootstrap loader
	 * @param clazz2 The called class.
	 * @param classPath2 Classpath used to load calling class. Only set when class is loaded by bootstrap loader
	 * @return the formatted message
	 */
	public static String formatNoSuchMethod(String methodSig, Class<?> clazz1, String classPath1, Class<?> clazz2, String classPath2) {
		String[] callingClassInfo = null;
		String[] calledClassInfo = null;
		try{
			callingClassInfo = getClassInfoStrings(clazz1, classPath1);
			calledClassInfo = getClassInfoStrings(clazz2, classPath2);

			String[] args = {
					methodSig, callingClassInfo[0], callingClassInfo[1],
					clazz2.toString(), calledClassInfo[0], calledClassInfo[1]
			};

			/*[MSG "K0613", "{0} (loaded from {1} by {2}) called from {3} (loaded from {4} by {5})."]*/
			return com.ibm.oti.util.Msg.getString(
					"K0613",	//$NON-NLS-1$
					"{0} (loaded from {1} by {2}) called from {3} (loaded from {4} by {5}).",	//$NON-NLS-1$
					args);
		} catch (Exception | VirtualMachineError e) {
			if ((null == callingClassInfo) || (null == calledClassInfo)) {
				return methodSig;
			}
			/* if retrieving the external message fails, hard code the message */
			try {
				return methodSig + "(loaded from " + callingClassInfo[0] + " by " + callingClassInfo[1] + ") called from " + clazz2.toString() +
						" (loaded from " + calledClassInfo[0] + " by " + calledClassInfo[1] + ")";
			} catch (Exception | VirtualMachineError e2) {
				/* if something fails, fall back to old message */
				return methodSig;
			}
		}

	}
}
