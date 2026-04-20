#include "compiler.h"
#include "tiny-jni.h"
#include "utils.h"
#include "vma_ops.h"

#include "rvvmlib.h"

#include "devices/riscv-aclint.h"
#include "devices/riscv-aplic.h"
#include "devices/riscv-imsic.h"
#include "devices/riscv-plic.h"

#include "devices/i2c-oc.h"
#include "devices/pci-bus.h"

#include "devices/framebuffer.h"
#include "devices/mtd-physmap.h"
#include "devices/ns16550a.h"
#include "devices/rtc-ds1742.h"
#include "devices/rtc-goldfish.h"
#include "devices/syscon.h"

#include "devices/nvme.h"
#include "devices/rtl8169.h"
#include "devices/sound-hda.h"

#include "devices/gpio-sifive.h"
#include "devices/hid_api.h"

PUSH_OPTIMIZATION_SIZE

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_check_1abi(JNIEnv* env, jclass cls, //
                                                                  jint abi)
{
    UNUSED(env);
    UNUSED(cls);
    return rvvm_check_abi(abi);
}

/*
 * RVVM Machine Management API
 */

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_create_1machine(JNIEnv* env, jclass cls, //
                                                                    jlong mem_size, jint smp, jstring isa)
{
    const char* u8_isa = (*env)->GetStringUTFChars(env, isa, NULL);
    UNUSED(cls);
    jlong ret = (size_t)rvvm_create_machine(mem_size, smp, u8_isa);
    (*env)->ReleaseStringUTFChars(env, isa, u8_isa);
    return ret;
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1cmdline(JNIEnv* env, jclass cls, //
                                                                jlong machine, jstring cmdline)
{
    const char* u8_cmdline = (*env)->GetStringUTFChars(env, cmdline, NULL);
    UNUSED(cls);
    rvvm_set_cmdline((rvvm_machine_t*)(size_t)machine, u8_cmdline);
    (*env)->ReleaseStringUTFChars(env, cmdline, u8_cmdline);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_append_1cmdline(JNIEnv* env, jclass cls, //
                                                                   jlong machine, jstring cmdline)
{
    const char* u8_cmdline = (*env)->GetStringUTFChars(env, cmdline, NULL);
    UNUSED(cls);
    rvvm_append_cmdline((rvvm_machine_t*)(size_t)machine, u8_cmdline);
    (*env)->ReleaseStringUTFChars(env, cmdline, u8_cmdline);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_load_1bootrom(JNIEnv* env, jclass cls, //
                                                                     jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool        ret     = rvvm_load_firmware((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(cls);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_load_1kernel(JNIEnv* env, jclass cls, //
                                                                    jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool        ret     = rvvm_load_kernel((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(cls);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_load_1dtb(JNIEnv* env, jclass cls, //
                                                                 jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool        ret     = rvvm_load_fdt((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(cls);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_dump_1dtb(JNIEnv* env, jclass cls, //
                                                                 jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool        ret     = rvvm_dump_fdt((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(cls);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1opt(JNIEnv* env, jclass cls, //
                                                             jlong machine, jint opt)
{
    UNUSED(env);
    UNUSED(cls);
    return rvvm_get_opt((rvvm_machine_t*)(size_t)machine, (uint32_t)opt);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1opt(JNIEnv* env, jclass cls, //
                                                            jlong machine, jint opt, jlong val)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_set_opt((rvvm_machine_t*)(size_t)machine, (uint32_t)opt, val);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_start_1machine(JNIEnv* env, jclass cls, //
                                                                      jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return rvvm_start_machine((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_pause_1machine(JNIEnv* env, jclass cls, //
                                                                      jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return rvvm_pause_machine((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_reset_1machine(JNIEnv* env, jclass cls, //
                                                                      jlong machine, jboolean reset)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_reset_machine((rvvm_machine_t*)(size_t)machine, reset);
    return true;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_machine_1running(JNIEnv* env, jclass cls, //
                                                                        jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return rvvm_machine_running((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_machine_1powered(JNIEnv* env, jclass cls, //
                                                                        jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return rvvm_machine_powered((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_free_1machine(JNIEnv* env, jclass cls, //
                                                                 jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_free_machine((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_run_1eventloop(JNIEnv* env, jclass cls)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_run_eventloop();
}

/*
 * RVVM Device API
 */

JNIEXPORT jobject JNICALL Java_lekkit_rvvm_RVVMNative_get_1dma_1buf(JNIEnv* env, jclass cls, //
                                                                    jlong machine, jlong addr, jlong size)
{
    void* ptr = rvvm_get_dma_ptr((rvvm_machine_t*)(size_t)machine, addr, size);
    UNUSED(cls);
    if (ptr == NULL) {
        return NULL;
    }
    return (*env)->NewDirectByteBuffer(env, ptr, size);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_mmio_1zone_1auto(JNIEnv* env, jclass cls, //
                                                                     jlong machine, jlong addr, jlong size)
{
    UNUSED(env);
    UNUSED(cls);
    return rvvm_mmio_zone_auto((rvvm_machine_t*)(size_t)machine, addr, size);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_remove_1mmio(JNIEnv* env, jclass cls, //
                                                                jlong mmio_dev)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_remove_mmio((rvvm_mmio_dev_t*)(size_t)mmio_dev);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1intc(JNIEnv* env, jclass cls, //
                                                              jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)rvvm_get_intc((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1intc(JNIEnv* env, jclass cls, //
                                                             jlong machine, jlong intc)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_set_intc((rvvm_machine_t*)(size_t)machine, (rvvm_intc_t*)(size_t)intc);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1pci_1bus(JNIEnv* env, jclass cls, //
                                                                  jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)rvvm_get_pci_bus((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1pci_1bus(JNIEnv* env, jclass cls, //
                                                                 jlong machine, jlong pci_bus)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_set_pci_bus((rvvm_machine_t*)(size_t)machine, (pci_bus_t*)(size_t)pci_bus);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1i2c_1bus(JNIEnv* env, jclass cls, //
                                                                  jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)rvvm_get_i2c_bus((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1i2c_1bus(JNIEnv* env, jclass cls, //
                                                                 jlong machine, jlong i2c_bus)
{
    UNUSED(env);
    UNUSED(cls);
    rvvm_set_i2c_bus((rvvm_machine_t*)(size_t)machine, (i2c_bus_t*)(size_t)i2c_bus);
}

/*
 * RVVM Devices
 */

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_riscv_1clint_1init_1auto(JNIEnv* env, jclass cls, //
                                                                            jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    riscv_clint_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_riscv_1imsic_1init_1auto(JNIEnv* env, jclass cls, //
                                                                            jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    riscv_imsic_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_riscv_1plic_1init_1auto(JNIEnv* env, jclass cls, //
                                                                            jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)riscv_plic_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_riscv_1aplic_1init_1auto(JNIEnv* env, jclass cls, //
                                                                             jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)riscv_aplic_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_pci_1bus_1init_1auto(JNIEnv* env, jclass cls, //
                                                                         jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)pci_bus_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_i2c_1bus_1init_1auto(JNIEnv* env, jclass cls, //
                                                                         jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)i2c_oc_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_tap_1user_1open(JNIEnv* env, jclass cls)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)tap_open();
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_syscon_1init_1auto(JNIEnv* env, jclass cls, //
                                                                       jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)syscon_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_rtc_1goldfish_1init_1auto(JNIEnv* env, jclass cls, //
                                                                              jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)rtc_goldfish_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_rtc_1ds1742_1init_1auto(JNIEnv* env, jclass cls, //
                                                                            jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)rtc_ds1742_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_ns16550a_1init_1auto(JNIEnv* env, jclass cls, //
                                                                         jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)ns16550a_init_term_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_gpio_1sifive_1init_1auto(JNIEnv* env, jclass cls, //
                                                                             jlong machine, jlong gpio)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)gpio_sifive_init_auto((rvvm_machine_t*)(size_t)machine, (rvvm_gpio_dev_t*)(size_t)gpio);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_mtd_1physmap_1init_1auto(JNIEnv* env, jclass cls, //
                                                                             jlong machine, jstring path, jboolean rw)
{
    const char*      u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    rvvm_mmio_dev_t* mmio    = mtd_physmap_init_auto((rvvm_machine_t*)(size_t)machine, u8_path, rw);
    UNUSED(cls);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return (size_t)mmio;
}

static void jni_framebuffer_remove(rvvm_mmio_dev_t* dev)
{
    rvvm_fbdev_dec_ref(dev->data);
    if (dev->mapping && dev->size) {
        vma_free(dev->mapping, dev->size);
    }
}

static const rvvm_mmio_type_t jni_framebuffer_dev_type = {
    .name   = "simple-framebuffer",
    .remove = jni_framebuffer_remove,
};

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_framebuffer_1init_1auto(JNIEnv* env, jclass cls, //
                                                                            jlong machine, jobjectArray fb, jint x,
                                                                            jint y, jint bpp)
{
    rvvm_fb_t fb_ctx = {
        .format = rvvm_rgb_from_bpp(bpp),
        .width  = x,
        .height = y,
    };
    UNUSED(cls);

    if (!rvvm_fb_size(&fb_ctx)) {
        rvvm_error("Invalid framebuffer size/bpp!");
        return 0;
    }

    fb_ctx.buffer = vma_alloc(NULL, rvvm_fb_size(&fb_ctx), VMA_RDWR);
    if (!fb_ctx.buffer) {
        rvvm_warn("Failed to allocate framebuffer via vma_alloc()!");
        return 0;
    }

    rvvm_fbdev_t* fbdev = rvvm_fbdev_init();
    rvvm_fbdev_set_vram(fbdev, rvvm_fb_buffer(&fb_ctx), rvvm_fb_size(&fb_ctx));
    rvvm_fbdev_set_scanout(fbdev, &fb_ctx);

    rvvm_mmio_dev_t* mmio = rvvm_simplefb_init_auto((rvvm_machine_t*)(size_t)machine, fbdev);
    if (mmio) {
        // Return direct ByteBuffer to Java side, register framebuffer cleanup callback
        jobject bytebuf = (*env)->NewDirectByteBuffer(env, rvvm_fb_buffer(&fb_ctx), rvvm_fb_size(&fb_ctx));
        mmio->type      = &jni_framebuffer_dev_type;

        if (!bytebuf) {
            rvvm_warn("Failed to create direct ByteBuffer for framebuffer!");
            return 0;
        }

        (*env)->SetObjectArrayElement(env, fb, 0, bytebuf);
        return (size_t)mmio;
    }

    return 0;
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_rtl8169_1init(JNIEnv* env, jclass cls, //
                                                                  jlong pci_bus, jlong tap)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)rtl8169_init((pci_bus_t*)(size_t)pci_bus, (tap_dev_t*)(size_t)tap);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_sound_1hda_1init_1auto(JNIEnv* env, jclass cls, //
                                                                           jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)sound_hda_init_auto((rvvm_machine_t*)(size_t)machine);
}

/*
 * JVM handle cached on first sound_hda_init_with_sink call. JNI guarantees
 * there's only one JVM per process, so one global is enough.
 */
static JavaVM* g_sound_jvm = NULL;

/*
 * Per-sink context. One allocated per SoundHDA device; lives for the
 * lifetime of the VM (freed via leak — matches the rest of sound-hda.c's
 * "devices live until machine destruction" pattern).
 */
typedef struct {
    jobject   sink_ref;      // Global ref to the Java sink object.
    jmethodID on_audio_mid;  // Cached method ID for onAudio([B)V.
} jni_sound_sink_ctx_t;

/*
 * Trampoline invoked by RVVM's HDA stream worker thread. Copies the PCM
 * chunk into a Java byte[] and calls sink.onAudio(bytes).
 *
 * Thread attachment: the first call on a given RVVM worker thread attaches
 * it to the JVM as a daemon. Subsequent calls reuse the attachment via
 * GetEnv. We never explicitly detach — the stream worker thread exits
 * naturally when the guest stops the stream, and the JVM cleans up the
 * attachment state at that point.
 */
static void jni_sound_sink_write(void* user_data, void* pcm_data, size_t size)
{
    jni_sound_sink_ctx_t* ctx = (jni_sound_sink_ctx_t*)user_data;
    if (ctx == NULL || g_sound_jvm == NULL) return;

    JNIEnv* env       = NULL;
    jint    get_result = (*g_sound_jvm)->GetEnv(g_sound_jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_result == JNI_EDETACHED) {
        // First call on this native thread — attach as daemon so it doesn't
        // block JVM shutdown. JNI docs promise the attachment survives until
        // the thread exits or explicitly detaches.
        if ((*g_sound_jvm)->AttachCurrentThreadAsDaemon(g_sound_jvm, (void**)&env, NULL) != JNI_OK) {
            return;
        }
    } else if (get_result != JNI_OK) {
        return;
    }

    // Java-side exception paranoia: if the sink's onAudio throws, we must
    // not let the exception propagate into the C side. Clear after each
    // call to keep the HDA worker thread healthy.
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)size);
    if (arr != NULL) {
        (*env)->SetByteArrayRegion(env, arr, 0, (jsize)size, (const jbyte*)pcm_data);
        (*env)->CallVoidMethod(env, ctx->sink_ref, ctx->on_audio_mid, arr);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, arr);
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

/*
 * Register a Java SoundSink callback that receives every PCM chunk the
 * guest writes to the HDA controller. The sink must expose a method with
 * signature `void onAudio(byte[] pcm)` — chunks arrive in whatever size
 * the guest's BDL entries dictate (typically 128-frame periods = 256 bytes
 * at 16-bit mono).
 *
 * Returns the pci_dev_t* handle (as jlong) for PCIDevice.setPCIHandle, or
 * 0 if attachment failed.
 */
JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_sound_1hda_1init_1with_1sink(
    JNIEnv* env, jclass cls, jlong machine, jobject sink)
{
    UNUSED(cls);
    if (sink == NULL) return 0;

    if (g_sound_jvm == NULL) {
        (*env)->GetJavaVM(env, &g_sound_jvm);
    }
    if (g_sound_jvm == NULL) return 0;

    jclass sink_class = (*env)->GetObjectClass(env, sink);
    if (sink_class == NULL) return 0;
    jmethodID on_audio_mid = (*env)->GetMethodID(env, sink_class, "onAudio", "([B)V");
    (*env)->DeleteLocalRef(env, sink_class);
    if (on_audio_mid == NULL) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        return 0;
    }

    jni_sound_sink_ctx_t* ctx = safe_new_obj(jni_sound_sink_ctx_t);
    ctx->sink_ref     = (*env)->NewGlobalRef(env, sink);
    ctx->on_audio_mid = on_audio_mid;

    pci_dev_t* dev = sound_hda_init_auto_ex((rvvm_machine_t*)(size_t)machine,
                                            jni_sound_sink_write,
                                            ctx);
    if (dev == NULL) {
        (*env)->DeleteGlobalRef(env, ctx->sink_ref);
        // ctx leaked — matches the existing pattern in sound-hda.c; revisit
        // if sound_hda_remove ever gets a proper teardown.
    }
    return (jlong)(size_t)dev;
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_nvme_1init(JNIEnv* env, jclass cls, //
                                                               jlong pci_bus, jstring path, jboolean rw)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    pci_dev_t*  ret     = nvme_init((pci_bus_t*)(size_t)pci_bus, u8_path, rw);
    UNUSED(cls);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return (size_t)ret;
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1init_1auto(JNIEnv* env, jclass cls, //
                                                                           jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)hid_mouse_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_hid_1keyboard_1init_1auto(JNIEnv* env, jclass cls, //
                                                                              jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    return (size_t)hid_keyboard_init_auto((rvvm_machine_t*)(size_t)machine);
}

static void jni_gpio_remove(rvvm_gpio_dev_t* gpio)
{
    free(gpio);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_gpio_1dev_1create(JNIEnv* env, jclass cls)
{
    rvvm_gpio_dev_t* gpio = safe_new_obj(rvvm_gpio_dev_t);
    gpio->remove          = jni_gpio_remove;
    UNUSED(env);
    UNUSED(cls);
    return (size_t)gpio;
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_pci_1remove_1device(JNIEnv* env, jclass cls, //
                                                                       jlong pci_dev)
{
    UNUSED(env);
    UNUSED(cls);
    pci_remove_device((pci_dev_t*)(size_t)pci_dev);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_gpio_1dev_1free(JNIEnv* env, jclass cls, //
                                                                   jlong gpio)
{
    void* ptr = (void*)(size_t)gpio;
    UNUSED(env);
    UNUSED(cls);
    free(ptr);
}

JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_gpio_1read_1pins(JNIEnv* env, jclass cls, //
                                                                    jlong gpio, jint off)
{
    UNUSED(env);
    UNUSED(cls);
    return gpio_read_pins((rvvm_gpio_dev_t*)(size_t)gpio, off);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_gpio_1write_1pins(JNIEnv* env, jclass cls, //
                                                                         jlong gpio, jint off, jint pins)
{
    UNUSED(env);
    UNUSED(cls);
    return gpio_write_pins((rvvm_gpio_dev_t*)(size_t)gpio, off, pins);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1resolution(JNIEnv* env, jclass cls, //
                                                                          jlong mice, jint x, jint y)
{
    UNUSED(env);
    UNUSED(cls);
    hid_mouse_resolution((hid_mouse_t*)(size_t)mice, x, y);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1place(JNIEnv* env, jclass cls, //
                                                                     jlong mice, jint x, jint y)
{
    UNUSED(env);
    UNUSED(cls);
    hid_mouse_place((hid_mouse_t*)(size_t)mice, x, y);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1move(JNIEnv* env, jclass cls, //
                                                                    jlong mice, jint x, jint y)
{
    UNUSED(env);
    UNUSED(cls);
    hid_mouse_move((hid_mouse_t*)(size_t)mice, x, y);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1press(JNIEnv* env, jclass cls, //
                                                                     jlong mice, jbyte btns)
{
    UNUSED(env);
    UNUSED(cls);
    hid_mouse_press((hid_mouse_t*)(size_t)mice, btns);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1release(JNIEnv* env, jclass cls, //
                                                                       jlong mice, jbyte btns)
{
    UNUSED(env);
    UNUSED(cls);
    hid_mouse_release((hid_mouse_t*)(size_t)mice, btns);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1scroll(JNIEnv* env, jclass cls, //
                                                                      jlong mice, jint offset)
{
    UNUSED(env);
    UNUSED(cls);
    hid_mouse_scroll((hid_mouse_t*)(size_t)mice, offset);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1keyboard_1press(JNIEnv* env, jclass cls, //
                                                                        jlong kb, jbyte key)
{
    UNUSED(env);
    UNUSED(cls);
    hid_keyboard_press((hid_keyboard_t*)(size_t)kb, key);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1keyboard_1release(JNIEnv* env, jclass cls, //
                                                                          jlong kb, jbyte key)
{
    UNUSED(env);
    UNUSED(cls);
    hid_keyboard_release((hid_keyboard_t*)(size_t)kb, key);
}

POP_OPTIMIZATION_SIZE
