#include <pthread.h>
#include <string.h>

#include "compiler.h"
#include "tiny-jni.h"
#include "utils.h"
#include "vma_ops.h"

#include "ringbuf.h"
#include "spinlock.h"
#include "atomics.h"

#include "rvvmlib.h"
#include "devices/chardev.h"

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
 * Audio sink design: native ring buffer + Java polling.
 *
 * Rationale: on JDK 21 / macOS arm64, AttachCurrentThread(AsDaemon) on an
 * RVVM-created pthread returns JNI_ERR. Rather than fight the JVM's thread
 * attach semantics, we stage PCM in a native mutex-guarded ring and have
 * Java poll it from a thread that's already JVM-owned (server tick).
 *
 * Layout:
 *   RVVM stream_worker thread --write-->  native ring buffer  <--poll-- Java server-tick thread
 *
 * Capacity: 1 MiB — ~2.7 seconds at 192 kHz mono 16-bit. If the Java
 * side falls behind more than that, the ring drops the oldest bytes
 * (latency over completeness, same philosophy as the stream worker's
 * under-run handling).
 */
#define JNI_SOUND_RING_BYTES (1u << 20)

typedef struct {
    pthread_mutex_t lock;
    uint8_t         buffer[JNI_SOUND_RING_BYTES];
    uint32_t        head;          // read position (next byte to read)
    uint32_t        tail;          // write position (next byte to write)
    uint32_t        count;         // bytes currently buffered
    uint64_t        total_pushed;  // monotonic counter of bytes ever written
    uint64_t        total_popped;  // monotonic counter of bytes ever read
    uint64_t        dropped;       // bytes dropped due to overflow
} jni_sound_ring_t;

static void jni_sound_ring_write(void* user_data, void* pcm_data, size_t size)
{
    jni_sound_ring_t* r = (jni_sound_ring_t*)user_data;
    if (r == NULL || pcm_data == NULL || size == 0) return;

    pthread_mutex_lock(&r->lock);
    // If we'd overflow, evict the oldest bytes first. Preserves head-of-stream
    // alignment better than dropping the incoming data.
    if (r->count + size > JNI_SOUND_RING_BYTES) {
        size_t to_evict = (r->count + size) - JNI_SOUND_RING_BYTES;
        if (to_evict > r->count) to_evict = r->count;
        r->head = (r->head + to_evict) % JNI_SOUND_RING_BYTES;
        r->count -= to_evict;
        r->dropped += to_evict;
    }
    const uint8_t* src = (const uint8_t*)pcm_data;
    uint32_t first = JNI_SOUND_RING_BYTES - r->tail;
    if (first > size) first = size;
    memcpy(r->buffer + r->tail, src, first);
    if (size > first) {
        memcpy(r->buffer, src + first, size - first);
    }
    r->tail = (r->tail + size) % JNI_SOUND_RING_BYTES;
    r->count += size;
    r->total_pushed += size;
    pthread_mutex_unlock(&r->lock);
}

static size_t jni_sound_ring_read(jni_sound_ring_t* r, uint8_t* out, size_t max)
{
    pthread_mutex_lock(&r->lock);
    size_t to_read = r->count < max ? r->count : max;
    uint32_t first = JNI_SOUND_RING_BYTES - r->head;
    if (first > to_read) first = to_read;
    memcpy(out, r->buffer + r->head, first);
    if (to_read > first) {
        memcpy(out + first, r->buffer, to_read - first);
    }
    r->head = (r->head + to_read) % JNI_SOUND_RING_BYTES;
    r->count -= to_read;
    r->total_popped += to_read;
    pthread_mutex_unlock(&r->lock);
    return to_read;
}

/*
 * Attach an HDA PCI device whose PCM output lands in a native ring
 * buffer. Returns a *sink handle* (pointer to jni_sound_ring_t) as jlong;
 * the PCI device handle is returned via {@code pci_dev_out[0]} so the
 * Java caller can hand it to {@code PCIDevice.setPCIHandle}.
 *
 * Two return values are awkward in JNI; a single-element jlong array is
 * the least-bad way to hand back the PCI device.
 *
 * Returns 0 on failure (sink handle); pci_dev_out[0] is then 0 too.
 */
JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_sound_1hda_1init_1with_1ring(
    JNIEnv* env, jclass cls, jlong machine, jlongArray pci_dev_out)
{
    UNUSED(cls);

    jni_sound_ring_t* ring = safe_new_obj(jni_sound_ring_t);
    if (pthread_mutex_init(&ring->lock, NULL) != 0) {
        return 0;
    }

    pci_dev_t* dev = sound_hda_init_auto_ex((rvvm_machine_t*)(size_t)machine,
                                            jni_sound_ring_write,
                                            ring);
    if (dev == NULL) {
        pthread_mutex_destroy(&ring->lock);
        // ring is leaked — matches the existing sound-hda.c pattern where
        // devices live until machine destruction with no cleanup.
        return 0;
    }

    if (pci_dev_out != NULL) {
        jlong out[1] = { (jlong)(size_t)dev };
        (*env)->SetLongArrayRegion(env, pci_dev_out, 0, 1, out);
    }
    return (jlong)(size_t)ring;
}

/*
 * Drain up to {@code out.length} bytes of queued PCM from the ring into
 * the supplied byte array. Returns the number of bytes actually read.
 * Called from Java on a JVM-owned thread (server tick) so no thread
 * attachment is needed.
 */
JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_sound_1hda_1poll(
    JNIEnv* env, jclass cls, jlong sink_handle, jbyteArray out)
{
    UNUSED(cls);
    if (sink_handle == 0 || out == NULL) return 0;
    jni_sound_ring_t* ring = (jni_sound_ring_t*)(size_t)sink_handle;
    jsize cap = (*env)->GetArrayLength(env, out);
    if (cap <= 0) return 0;

    // Stack-allocate small chunks, heap for larger (ring buffer is up to
    // 1 MiB so Java-side polls typically ask for tens of KB). Use a
    // temporary local buffer to avoid holding the ring mutex during JNI
    // array ops.
    uint8_t* tmp = safe_new_arr(uint8_t, (size_t)cap);
    size_t n = jni_sound_ring_read(ring, tmp, (size_t)cap);
    if (n > 0) {
        (*env)->SetByteArrayRegion(env, out, 0, (jsize)n, (const jbyte*)tmp);
    }
    free(tmp);
    return (jint)n;
}

/* Stats — monotonic counters useful for tests and instrumentation. */
JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_sound_1hda_1stats(
    JNIEnv* env, jclass cls, jlong sink_handle, jint which)
{
    UNUSED(env); UNUSED(cls);
    if (sink_handle == 0) return 0;
    jni_sound_ring_t* ring = (jni_sound_ring_t*)(size_t)sink_handle;
    pthread_mutex_lock(&ring->lock);
    jlong v = 0;
    switch (which) {
        case 0: v = (jlong)ring->total_pushed; break;
        case 1: v = (jlong)ring->total_popped; break;
        case 2: v = (jlong)ring->dropped;      break;
        case 3: v = (jlong)ring->count;        break;
        default: v = 0;
    }
    pthread_mutex_unlock(&ring->lock);
    return v;
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

/*
 * NS16550A JNI bridge
 *
 * A `chardev_t` backed by two spinlock-guarded ring buffers. Instead of
 * talking to stdio (chardev_term) or a pty (chardev_pty), the UART talks to
 * Java: guest TX bytes go into a `tx` ring the Java side drains via
 * `ns16550a_bridge_poll`, and Java pushes bytes into an `rx` ring via
 * `ns16550a_bridge_feed` which the UART consumes.
 *
 * Threading: chardev read/write/poll/update runs on the RVVM event loop or
 * CPU thread; JNI feed/poll runs on whatever JVM thread calls in. Ring
 * access is guarded by `lock`; chardev_notify happens outside the lock to
 * avoid re-entering the UART while holding it.
 *
 * Overflow policy: guest TX that outruns Java draining drops oldest bytes
 * (same philosophy as the HDA ring in feat/sound-backend-api — preserve
 * latency and head alignment over completeness). Java-fed RX that outruns
 * the guest reading reports short writes; caller retries.
 */

#define JNI_UART_RING_SIZE 65536

typedef struct {
    chardev_t  chardev;
    ringbuf_t  rx;              // Java -> guest (UART reads from here)
    ringbuf_t  tx;              // guest -> Java (UART writes here)
    spinlock_t lock;
    uint32_t   flags;           // CHARDEV_RX | CHARDEV_TX cache

    uint64_t   total_pushed;    // bytes guest has written into tx
    uint64_t   total_popped;    // bytes Java has drained from tx
    uint64_t   total_fed;       // bytes Java has written into rx
    uint64_t   total_consumed;  // bytes guest has read from rx
    uint64_t   tx_dropped;      // bytes dropped on tx overflow
} jni_uart_bridge_t;

// Recompute flags; return the bits that newly became set (for notify delta).
static uint32_t jni_uart_update_flags(jni_uart_bridge_t* b)
{
    uint32_t flags = 0;
    uint32_t prev  = atomic_load_uint32_relax(&b->flags);
    if (ringbuf_avail(&b->rx)) {
        flags |= CHARDEV_RX;
    }
    if (ringbuf_space(&b->tx)) {
        flags |= CHARDEV_TX;
    }
    atomic_store_uint32_relax(&b->flags, flags);
    return flags & ~prev;
}

static uint32_t jni_uart_poll(chardev_t* dev)
{
    jni_uart_bridge_t* b = dev->data;
    return atomic_load_uint32_relax(&b->flags);
}

static size_t jni_uart_read(chardev_t* dev, void* buf, size_t nbytes)
{
    jni_uart_bridge_t* b   = dev->data;
    size_t             ret = 0;
    scoped_spin_lock (&b->lock) {
        ret = ringbuf_read(&b->rx, buf, nbytes);
        b->total_consumed += ret;
        jni_uart_update_flags(b);
    }
    return ret;
}

static size_t jni_uart_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    jni_uart_bridge_t* b = dev->data;
    scoped_spin_lock (&b->lock) {
        size_t space = ringbuf_space(&b->tx);
        if (nbytes > space) {
            size_t overflow = nbytes - space;
            size_t avail    = ringbuf_avail(&b->tx);
            size_t drop     = overflow > avail ? avail : overflow;
            ringbuf_skip(&b->tx, drop);
            b->tx_dropped += drop;
        }
        ringbuf_write(&b->tx, buf, nbytes);
        b->total_pushed += nbytes;
        jni_uart_update_flags(b);
    }
    // We always "accept" everything — overflow is absorbed by dropping old
    // bytes. Matches the UART's expectation that writes don't back-pressure.
    return nbytes;
}

static void jni_uart_update(chardev_t* dev)
{
    // Nothing to pump — our rings are fed and drained by Java, not by the
    // event loop. Just recompute flags and notify on edge.
    jni_uart_bridge_t* b     = dev->data;
    uint32_t           delta = 0;
    scoped_spin_lock (&b->lock) {
        delta = jni_uart_update_flags(b);
    }
    if (delta) {
        chardev_notify(&b->chardev, atomic_load_uint32_relax(&b->flags));
    }
}

static void jni_uart_remove(chardev_t* dev)
{
    jni_uart_bridge_t* b = dev->data;
    ringbuf_destroy(&b->rx);
    ringbuf_destroy(&b->tx);
    free(b);
}

static jni_uart_bridge_t* jni_uart_bridge_create(void)
{
    jni_uart_bridge_t* b = safe_new_obj(jni_uart_bridge_t);
    ringbuf_create(&b->rx, JNI_UART_RING_SIZE);
    ringbuf_create(&b->tx, JNI_UART_RING_SIZE);
    // TX is always "writable" while we have space; RX becomes set when fed.
    b->flags             = CHARDEV_TX;
    b->chardev.data      = b;
    b->chardev.poll      = jni_uart_poll;
    b->chardev.read      = jni_uart_read;
    b->chardev.write     = jni_uart_write;
    b->chardev.update    = jni_uart_update;
    b->chardev.remove    = jni_uart_remove;
    return b;
}

/*
 * Attach an NS16550A wired to a JNI bridge. Returns a handle to the bridge
 * (NOT the MMIO dev) — the MMIO dev's lifetime is tied to the machine and
 * will call our chardev's remove() when the machine is freed.
 *
 * Returns 0 on failure.
 */
JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_ns16550a_1bridge_1init(JNIEnv* env, jclass cls, //
                                                                           jlong machine)
{
    UNUSED(env);
    UNUSED(cls);
    jni_uart_bridge_t* b    = jni_uart_bridge_create();
    rvvm_mmio_dev_t*   mmio = ns16550a_init_auto((rvvm_machine_t*)(size_t)machine, &b->chardev);
    if (mmio == NULL) {
        // Attach failed — chardev_free would call our remove which frees b.
        chardev_free(&b->chardev);
        return 0;
    }
    return (jlong)(size_t)b;
}

/*
 * Drain up to `out.length` bytes of guest TX into the provided byte[].
 * Returns the number of bytes written. Non-blocking.
 */
JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_ns16550a_1bridge_1poll(JNIEnv* env, jclass cls, //
                                                                          jlong handle, jbyteArray out)
{
    UNUSED(cls);
    if (handle == 0 || out == NULL) {
        return 0;
    }
    jni_uart_bridge_t* b   = (jni_uart_bridge_t*)(size_t)handle;
    jsize              cap = (*env)->GetArrayLength(env, out);
    if (cap <= 0) {
        return 0;
    }

    jbyte* buf = (*env)->GetByteArrayElements(env, out, NULL);
    if (buf == NULL) {
        return 0;
    }

    size_t   got   = 0;
    uint32_t delta = 0;
    scoped_spin_lock (&b->lock) {
        got = ringbuf_read(&b->tx, buf, cap);
        b->total_popped += got;
        delta = jni_uart_update_flags(b);
    }
    (*env)->ReleaseByteArrayElements(env, out, buf, 0);

    // TX space may have become available — wake the UART so its next write
    // path re-evaluates. In practice the UART re-polls on every write anyway,
    // but this keeps edge-triggered IRQ paths honest.
    if (delta & CHARDEV_TX) {
        chardev_notify(&b->chardev, atomic_load_uint32_relax(&b->flags));
    }
    return (jint)got;
}

/*
 * Push up to `in.length` bytes into guest RX. Returns how many were actually
 * accepted (may be less than length if the RX ring was near-full).
 */
JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_ns16550a_1bridge_1feed(JNIEnv* env, jclass cls, //
                                                                          jlong handle, jbyteArray in)
{
    UNUSED(cls);
    if (handle == 0 || in == NULL) {
        return 0;
    }
    jni_uart_bridge_t* b   = (jni_uart_bridge_t*)(size_t)handle;
    jsize              len = (*env)->GetArrayLength(env, in);
    if (len <= 0) {
        return 0;
    }

    jbyte* buf = (*env)->GetByteArrayElements(env, in, NULL);
    if (buf == NULL) {
        return 0;
    }

    size_t   put   = 0;
    uint32_t delta = 0;
    scoped_spin_lock (&b->lock) {
        put = ringbuf_write(&b->rx, buf, len);
        b->total_fed += put;
        delta = jni_uart_update_flags(b);
    }
    (*env)->ReleaseByteArrayElements(env, in, buf, JNI_ABORT);

    if (delta & CHARDEV_RX) {
        chardev_notify(&b->chardev, atomic_load_uint32_relax(&b->flags));
    }
    return (jint)put;
}

/*
 * Fill a long[5] with {total_pushed, total_popped, total_fed, total_consumed,
 * tx_dropped}. Skips if the array is null or too short.
 */
JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_ns16550a_1bridge_1stats(JNIEnv* env, jclass cls, //
                                                                           jlong handle, jlongArray out)
{
    UNUSED(cls);
    if (handle == 0 || out == NULL) {
        return;
    }
    jni_uart_bridge_t* b = (jni_uart_bridge_t*)(size_t)handle;
    if ((*env)->GetArrayLength(env, out) < 5) {
        return;
    }
    jlong stats[5];
    scoped_spin_lock (&b->lock) {
        stats[0] = (jlong)b->total_pushed;
        stats[1] = (jlong)b->total_popped;
        stats[2] = (jlong)b->total_fed;
        stats[3] = (jlong)b->total_consumed;
        stats[4] = (jlong)b->tx_dropped;
    }
    (*env)->SetLongArrayRegion(env, out, 0, 5, stats);
}

POP_OPTIMIZATION_SIZE
