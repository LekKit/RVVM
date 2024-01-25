#include "tiny-jni.h"
#include "utils.h"
#include "rvvmlib.h"
#include "devices/clint.h"
#include "devices/plic.h"
#include "devices/pci-bus.h"
#include "devices/i2c-oc.h"
#include "devices/ns16550a.h"
#include "devices/rtc-goldfish.h"
#include "devices/syscon.h"
#include "devices/rtl8169.h"
#include "devices/nvme.h"
#include "devices/mtd-physmap.h"
#include "devices/framebuffer.h"
#include "devices/hid_api.h"

JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_get_1abi_1version(JNIEnv* env, jclass class)
{
    UNUSED(env); UNUSED(class);
    return RVVM_ABI_VERSION;
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_create_1machine(JNIEnv* env, jclass class, jlong mem_base, jlong mem_size, jint smp, jboolean rv64)
{
    UNUSED(env); UNUSED(class);
    return (size_t)rvvm_create_machine(mem_base, mem_size, smp, rv64);
}

JNIEXPORT jobject JNICALL Java_lekkit_rvvm_RVVMNative_get_1dma_1buf(JNIEnv* env, jclass class, jlong machine, jlong addr, jlong size)
{
    void* ptr = rvvm_get_dma_ptr((rvvm_machine_t*)(size_t)machine, addr, size);
    UNUSED(class);
    if (ptr == NULL) return NULL;
    return (*env)->NewDirectByteBuffer(env, ptr, size);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1plic(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)rvvm_get_plic((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1plic(JNIEnv* env, jclass class, jlong machine, jlong plic)
{
    UNUSED(env); UNUSED(class);
    rvvm_set_plic((rvvm_machine_t*)(size_t)machine, (plic_ctx_t*)(size_t)plic);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1pci_1bus(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)rvvm_get_pci_bus((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1pci_1bus(JNIEnv* env, jclass class, jlong machine, jlong pci_bus)
{
    UNUSED(env); UNUSED(class);
    rvvm_set_pci_bus((rvvm_machine_t*)(size_t)machine, (pci_bus_t*)(size_t)pci_bus);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1i2c_1bus(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)rvvm_get_i2c_bus((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1i2c_1bus(JNIEnv* env, jclass class, jlong machine, jlong i2c_bus)
{
    UNUSED(env); UNUSED(class);
    rvvm_set_i2c_bus((rvvm_machine_t*)(size_t)machine, (i2c_bus_t*)(size_t)i2c_bus);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1cmdline(JNIEnv* env, jclass class, jlong machine, jstring cmdline)
{
    const char* u8_cmdline = (*env)->GetStringUTFChars(env, cmdline, NULL);
    UNUSED(class);
    rvvm_set_cmdline((rvvm_machine_t*)(size_t)machine, u8_cmdline);
    (*env)->ReleaseStringUTFChars(env, cmdline, u8_cmdline);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_append_1cmdline(JNIEnv* env, jclass class, jlong machine, jstring cmdline)
{
    const char* u8_cmdline = (*env)->GetStringUTFChars(env, cmdline, NULL);
    UNUSED(class);
    rvvm_append_cmdline((rvvm_machine_t*)(size_t)machine, u8_cmdline);
    (*env)->ReleaseStringUTFChars(env, cmdline, u8_cmdline);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_get_1opt(JNIEnv* env, jclass class, jlong machine, jint opt)
{
    UNUSED(env); UNUSED(class);
    return rvvm_get_opt((rvvm_machine_t*)(size_t)machine, (uint32_t)opt);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_set_1opt(JNIEnv* env, jclass class, jlong machine, jint opt, jlong val)
{
    UNUSED(env); UNUSED(class);
    rvvm_set_opt((rvvm_machine_t*)(size_t)machine, (uint32_t)opt, val);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_load_1bootrom(JNIEnv* env, jclass class, jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool ret = rvvm_load_bootrom((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(class);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_load_1kernel(JNIEnv* env, jclass class, jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool ret = rvvm_load_kernel((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(class);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_load_1dtb(JNIEnv* env, jclass class, jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool ret = rvvm_load_dtb((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(class);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_dump_1dtb(JNIEnv* env, jclass class, jlong machine, jstring path)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    bool ret = rvvm_dump_dtb((rvvm_machine_t*)(size_t)machine, u8_path);
    UNUSED(class);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_start_1machine(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return rvvm_start_machine((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_pause_1machine(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return rvvm_pause_machine((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_reset_1machine(JNIEnv* env, jclass class, jlong machine, jboolean reset)
{
    UNUSED(env); UNUSED(class);
    rvvm_reset_machine((rvvm_machine_t*)(size_t)machine, reset);
    return true;
}

JNIEXPORT jboolean JNICALL Java_lekkit_rvvm_RVVMNative_machine_1powered(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return rvvm_machine_powered((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_free_1machine(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    rvvm_free_machine((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_detach_1mmio(JNIEnv* env, jclass class, jlong machine, jint handle, jboolean cleanup)
{
    UNUSED(env); UNUSED(class);
    rvvm_detach_mmio((rvvm_machine_t*)(size_t)machine, handle, cleanup);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_run_1eventloop(JNIEnv* env, jclass class)
{
    UNUSED(env); UNUSED(class);
    rvvm_run_eventloop();
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_clint_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    clint_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_plic_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)plic_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_pci_1bus_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)pci_bus_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_i2c_1bus_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)i2c_oc_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_ns16550a_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    ns16550a_init_term_auto((rvvm_machine_t*)(size_t)machine);
    return 0;
}

JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_rtc_1goldfish_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    rtc_goldfish_init_auto((rvvm_machine_t*)(size_t)machine);
    return 0;
}

JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_syscon_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    syscon_init_auto((rvvm_machine_t*)(size_t)machine);
    return 0;
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_rtl8169_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)rtl8169_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_nvme_1init_1auto(JNIEnv* env, jclass class, jlong machine, jstring path, jboolean rw)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    pci_dev_t* ret = nvme_init_auto((rvvm_machine_t*)(size_t)machine, u8_path, rw);
    UNUSED(class);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return (size_t)ret;
}

JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_mtd_1physmap_1init_1auto(JNIEnv* env, jclass class, jlong machine, jstring path, jboolean rw)
{
    const char* u8_path = (*env)->GetStringUTFChars(env, path, NULL);
    rvvm_mmio_handle_t ret = mtd_physmap_init_auto((rvvm_machine_t*)(size_t)machine, u8_path, rw);
    UNUSED(class);
    (*env)->ReleaseStringUTFChars(env, path, u8_path);
    return ret;
}

JNIEXPORT jint JNICALL Java_lekkit_rvvm_RVVMNative_framebuffer_1init_1auto(JNIEnv* env, jclass class, jlong machine, jobject fb, jint x, jint y, jint bpp)
{
    size_t buf_size = (*env)->GetDirectBufferCapacity(env, fb);
    fb_ctx_t fb_ctx = {
        .buffer = (*env)->GetDirectBufferAddress(env, fb),
        .format = rgb_format_from_bpp(bpp),
        .width = x,
        .height = y,
    };
    UNUSED(class);
    if (fb_ctx.buffer && framebuffer_size(&fb_ctx) == buf_size) {
        framebuffer_init_auto((rvvm_machine_t*)(size_t)machine, &fb_ctx);
        return 0;
    } else {
        rvvm_warn("Invalid ByteBuffer passed to JNI framebuffer_init_auto()");
        return RVVM_INVALID_MMIO;
    }
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)hid_mouse_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT jlong JNICALL Java_lekkit_rvvm_RVVMNative_hid_1keyboard_1init_1auto(JNIEnv* env, jclass class, jlong machine)
{
    UNUSED(env); UNUSED(class);
    return (size_t)hid_keyboard_init_auto((rvvm_machine_t*)(size_t)machine);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_pci_1remove_1device(JNIEnv* env, jclass class, jlong pci_dev)
{
    UNUSED(env); UNUSED(class);
    pci_remove_device((pci_dev_t*)(size_t)pci_dev);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1place(JNIEnv* env, jclass class, jlong mice, jint x, jint y)
{
    UNUSED(env); UNUSED(class);
    hid_mouse_place((hid_mouse_t*)(size_t)mice, x, y);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1move(JNIEnv* env, jclass class, jlong mice, jint x, jint y)
{
    UNUSED(env); UNUSED(class);
    hid_mouse_move((hid_mouse_t*)(size_t)mice, x, y);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1press(JNIEnv* env, jclass class, jlong mice, jbyte btns)
{
    UNUSED(env); UNUSED(class);
    hid_mouse_press((hid_mouse_t*)(size_t)mice, btns);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1release(JNIEnv* env, jclass class, jlong mice, jbyte btns)
{
    UNUSED(env); UNUSED(class);
    hid_mouse_release((hid_mouse_t*)(size_t)mice, btns);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1mouse_1scroll(JNIEnv* env, jclass class, jlong mice, jint offset)
{
    UNUSED(env); UNUSED(class);
    hid_mouse_scroll((hid_mouse_t*)(size_t)mice, offset);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1keyboard_1press(JNIEnv* env, jclass class, jlong kb, jbyte key)
{
    UNUSED(env); UNUSED(class);
    hid_keyboard_press((hid_keyboard_t*)(size_t)kb, key);
}

JNIEXPORT void JNICALL Java_lekkit_rvvm_RVVMNative_hid_1keyboard_1release(JNIEnv* env, jclass class, jlong kb, jbyte key)
{
    UNUSED(env); UNUSED(class);
    hid_keyboard_release((hid_keyboard_t*)(size_t)kb, key);
}
