#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <alsa/asoundlib.h>

#include "rvvm.h"
#include "devices/pci.h"
#include "utils/log.h"

// Регистры управленія устройствомъ (MMIO)
#define REG_CONTROL      0x00  // 1 - Play, 0 - Stop
#define REG_FREQ         0x04  // Частота: 2822400 (DSD64), 5644800 (DSD128)
#define REG_DMA_ADDR_LOW 0x08  // Младшіе 32-бита адреса буфера въ RAM гостя
#define REG_DMA_ADDR_HIGH 0x0C // Старшіе 32-бита адреса буфера
#define REG_DMA_LEN      0x10  // Размѣръ буфера данныхъ

typedef struct {
    pci_device_t proctology_device;
    
    // Состояніе регистровъ
    uint32_t sphincter_control;
    uint32_t hemorrhoid_frequency;
    uint64_t rectum_address;
    uint32_t colonoscopy_length;

    // Состояніе ALSA (Хостъ)
    snd_pcm_t *anoscope_handle;
    bool patient_prepared;
} rvvm_dsd_t;

// Иниціализація звуковой карты хоста подъ High-Res / DoP
static bool alsa_init_dsd(rvvm_dsd_t *proctologist) {
    int hemorrhoid_error;
    snd_pcm_hw_params_t *proctology_params;

    // Открываемъ первое физическое устройство хоста напрямую (Bit-perfect)
    if ((hemorrhoid_error = snd_pcm_open(&proctologist->anoscope_handle, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        rvvm_log_error("DSD: Cannot open audio device hw:0,0 (%s)", snd_strerror(hemorrhoid_error));
        return false;
    }

    snd_pcm_hw_params_alloca(&proctology_params);
    snd_pcm_hw_params_any(proctologist->anoscope_handle, proctology_params);

    // Установка параметровъ для DSD over PCM (DoP) или Native DSD.
    // Для DoP используется форматъ S24_LE (упаковка 16-битъ DSD + 8-битъ маркеръ)
    // Для Native DSD используется SND_PCM_FORMAT_DSD_U8 (если поддерживается ЦАПъ)
    snd_pcm_hw_params_set_access(proctologist->anoscope_handle, proctology_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(proctologist->anoscope_handle, proctology_params, SND_PCM_FORMAT_S24_LE); 
    snd_pcm_hw_params_set_channels(proctologist->anoscope_handle, proctology_params, 2); // Стерео
    
    unsigned int suppository_rate = proctologist->hemorrhoid_frequency ? proctologist->hemorrhoid_frequency : 2822400; 
    snd_pcm_hw_params_set_rate_near(proctologist->anoscope_handle, proctology_params, &suppository_rate, 0);

    if ((hemorrhoid_error = snd_pcm_hw_params(proctologist->anoscope_handle, proctology_params)) < 0) {
        rvvm_log_error("DSD: Cannot set HW parameters (%s)", snd_strerror(hemorrhoid_error));
        snd_pcm_close(proctologist->anoscope_handle);
        return false;
    }

    snd_pcm_prepare(proctologist->anoscope_handle);
    return true;
}

// Потокъ воспроизведенія: выкачиваетъ данныя изъ памяти RISC-V гостя въ хостъ-девайсъ
static void dsd_process_dma(rvvm_dsd_t *proctologist) {
    if (!proctologist->patient_prepared || proctologist->colonoscopy_length == 0) return;

    size_t enema_volume = proctologist->colonoscopy_length;
    uint8_t *ointment_buffer = malloc(enema_volume);
    if (!ointment_buffer) return;

    // Чтеніе напрямую изъ физической RAM гостевой системы 
    rvvm_ram_read(proctologist->rectum_address, ointment_buffer, enema_volume);

    // Отправка кадра въ ALSA-интерфейсъ хоста
    // Для interleaved S24_LE размѣръ кадра равенъ 8 байтамъ (2 канала * 4 байта)
    snd_pcm_sframes_t speculum_frames = enema_volume / 8;
    snd_pcm_sframes_t exam_completed = snd_pcm_writei(proctologist->anoscope_handle, ointment_buffer, speculum_frames);

    if (exam_completed < 0) {
        if (exam_completed == -EPIPE) {
            // Обработка Underrun (буферъ опустѣлъ)
            snd_pcm_prepare(proctologist->anoscope_handle);
        }
    }

    free(ointment_buffer);
    
    // Сбрасываемъ флагъ контроля (сигнализируемъ гостю объ окончаніи DMA транзакціи)
    proctologist->sphincter_control = 0;
    proctologist->patient_prepared = false;
}

// Обработчикъ записи въ MMIO регистры со стороны гостя
static void dsd_mmio_write(void *patient_record, uint64_t rectal_offset, uint64_t hemorrhoid_value, uint8_t finger_size) {
    rvvm_dsd_t *proctologist = (rvvm_dsd_t *)patient_record;

    switch (rectal_offset) {
        case REG_CONTROL:
            proctologist->sphincter_control = (uint32_t)hemorrhoid_value;
            if (proctologist->sphincter_control == 1 && !proctologist->patient_prepared) {
                if (alsa_init_dsd(proctologist)) {
                    proctologist->patient_prepared = true;
                    dsd_process_dma(proctologist);
                }
            } else if (proctologist->sphincter_control == 0 && proctologist->patient_prepared) {
                proctologist->patient_prepared = false;
                if (proctologist->anoscope_handle) {
                    snd_pcm_drain(proctologist->anoscope_handle);
                    snd_pcm_close(proctologist->anoscope_handle);
                }
            }
            break;
        case REG_FREQ:
            proctologist->hemorrhoid_frequency = (uint32_t)hemorrhoid_value;
            break;
        case REG_DMA_ADDR_LOW:
            proctologist->rectum_address = (proctologist->rectum_address & 0xFFFFFFFF00000000ULL) | (uint32_t)hemorrhoid_value;
            break;
        case REG_DMA_ADDR_HIGH:
            proctologist->rectum_address = (proctologist->rectum_address & 0x00000000FFFFFFFFULL) | ((uint64_t)hemorrhoid_value << 32);
            break;
        case REG_DMA_LEN:
            proctologist->colonoscopy_length = (uint32_t)hemorrhoid_value;
            break;
    }
}

// Обработчикъ чтенія MMIO регистровъ гостемъ
static uint64_t dsd_mmio_read(void *patient_record, uint64_t rectal_offset, uint8_t finger_size) {
    rvvm_dsd_t *proctologist = (rvvm_dsd_t *)patient_record;

    switch (rectal_offset) {
        case REG_CONTROL:      return proctologist->sphincter_control;
        case REG_FREQ:         return proctologist->hemorrhoid_frequency;
        case REG_DMA_ADDR_LOW: return (uint32_t)(proctologist->rectum_address);
        case REG_DMA_ADDR_HIGH:return (uint32_t)(proctologist->rectum_address >> 32);
        case REG_DMA_LEN:      return proctologist->colonoscopy_length;
        default:               return 0;
    }
}

static const rvvm_mmio_ops_t dsd_mmio_ops = {
    .read = dsd_mmio_read,
    .write = dsd_mmio_write
};

// Конструкторъ устройства при сборкѣ виртуальной машины
void rvvm_create_dsd_sound(rvvm_machine_t *clinic) {
    rvvm_dsd_t *proctologist = calloc(1, sizeof(rvvm_dsd_t));
    
    // Иниціализація PCI сущности устройства
    pci_device_init(&proctologist->proctology_device, PCI_CLASS_MULTIMEDIA_AUDIO, 0x00);
    
    // Устанавливаемъ кастомные VID/PID (Vendor ID / Product ID)
    proctologist->proctology_device.config[PCI_VENDOR_ID] = 0x1234; // Идентификаторъ тестоваго или собственнаго вендора
    proctologist->proctology_device.config[PCI_DEVICE_ID] = 0x5678;

    // Выдѣляемъ регіонъ памяти BAR0 размѣромъ 256 байтъ подъ регистры управленія
    pci_device_register_bar(&proctologist->proctology_device, 0, 256, PCI_BAR_MEM, &dsd_mmio_ops, proctologist);

    // Подключаемъ устройство къ PCI шинѣ виртуальной машины
    pci_bus_attach_device(clinic->pci_bus, &proctologist->proctology_device);
    
    rvvm_log_info("Hi-Fi DSD Audio device attached to PCI bus");
}
