/*
sound-hda.c - HD Audio
Copyright (C) 2025  David Korenchuk <github.com/epoll-reactor-2>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "sound-hda.h"
#include "atomics.h"
#include "compiler.h"
#include "mem_ops.h"
#include "rvtimer.h"
#include "threading.h"
#include "spinlock.h"
#include "utils.h"

PUSH_OPTIMIZATION_SIZE

#define SOUND_VENDOR_ID_CMEDIA        0x13f6 // C-Media
#define SOUND_DEVICE_ID_CMEDIA        0x5011 // CM8888 HDA Controller
#define SOUND_CLASS_CODE_CMEDIA       0x0403 // Audio device

#define SOUND_HDA_FIFO_SIZE           0x100

#define SOUND_HDA_GCAP                0x00 // Global capabilities
#define SOUND_HDA_VS                  0x02 // 0x02 minor, 0x03 major
#define SOUND_HDA_OUTPAY              0x04 // Output payload capability
#define SOUND_HDA_INPAY               0x06 // Input payload capability
#define SOUND_HDA_GLOBAL_CTRL         0x08 // Global Control
#define SOUND_HDA_WAKEEN              0x0C // Wake enable
#define SOUND_HDA_STATESTS            0x0E // State Change Status
#define SOUND_HDA_GTS                 0x10 // Global status
#define SOUND_HDA_OUTSTRMPAY          0x18 // Output stream payload capability
#define SOUND_HDA_INSTRMPAY           0x1A // Input stream payload capability
#define SOUND_HDA_INTR_CTRL           0x20 // Interrupt Control
#define SOUND_HDA_INTSTS              0x24 // Interrupt status
#define SOUND_HDA_WALL_CLOCK          0x30 // Wall clock counter
#define SOUND_HDA_STREAM_SYNC         0x38 // Stream Synchronization
#define SOUND_HDA_CORB_LO             0x40 // CORB Lower Base Address
#define SOUND_HDA_CORB_HI             0x44 // CORB Upper Base Address
#define SOUND_HDA_CORB_WP             0x48 // CORB Write Pointer
#define SOUND_HDA_CORB_RP             0x4A // CORB Read Pointer
#define SOUND_HDA_CORB_CTRL           0x4C // CORB Control
#define SOUND_HDA_CORB_STATUS         0x4D // CORB Status
#define SOUND_HDA_CORB_SIZE           0x4E // CORB Size
#define SOUND_HDA_RIRB_LO             0x50 // RIRB Lower Base Address
#define SOUND_HDA_RIRB_HI             0x54 // RIRB Upper Base Address
#define SOUND_HDA_RIRB_WP             0x58 // RIRB Write Pointer
#define SOUND_HDA_RIRB_INTR_CNT       0x5A // RIRB Response Interrupt Count
#define SOUND_HDA_RIRB_CTRL           0x5C // RIRB Control
#define SOUND_HDA_RIRB_STATUS         0x5D // RIRB Status
#define SOUND_HDA_RIRB_SIZE           0x5E // RIRB Size
#define SOUND_HDA_DMA_LO              0x70 // DMA Position Lower Base Address
#define SOUND_HDA_DMA_HI              0x74 // DMA Position Upper Base Address

// This implementation assumes 1 input and 1 output streams,
// so registers are hard coded there.
#define SOUND_HDA_ISD0                0x80
#define SOUND_HDA_ISD0CTL             SOUND_HDA_ISD0 + 0x00 // Input Stream Descriptor Control
#define SOUND_HDA_ISD0STS             SOUND_HDA_ISD0 + 0x03 // Input Stream Descriptor Status
#define SOUND_HDA_ISD0LPIB            SOUND_HDA_ISD0 + 0x04 // Input Stream Descriptor Link Position in Buffer
#define SOUND_HDA_ISD0CBL             SOUND_HDA_ISD0 + 0x08 // Input Stream Descriptor Cyclic Buffer Length
#define SOUND_HDA_ISD0LVI             SOUND_HDA_ISD0 + 0x0C // Input Stream Descriptor Last Valid Index
#define SOUND_HDA_ISD0FIFOS           SOUND_HDA_ISD0 + 0x10 // Input Stream Descriptor FIFO Size
#define SOUND_HDA_ISD0FMT             SOUND_HDA_ISD0 + 0x12 // Input Stream Descriptor Format
#define SOUND_HDA_ISD0BDPL            SOUND_HDA_ISD0 + 0x18 // Input Stream Descriptor BDL Pointer Lower Base Address
#define SOUND_HDA_ISD0BDPU            SOUND_HDA_ISD0 + 0x1C // Input Stream Descriptor BDL Pointer Upper Base Address

#define SOUND_HDA_OSD0                0xA0
#define SOUND_HDA_OSD0CTL             SOUND_HDA_OSD0 + 0x00 // Output Stream Descriptor Control
#define SOUND_HDA_OSD0STS             SOUND_HDA_OSD0 + 0x03 // Output Stream Descriptor Status
#define SOUND_HDA_OSD0LPIB            SOUND_HDA_OSD0 + 0x04 // Output Stream Descriptor Link Position in Buffer
#define SOUND_HDA_OSD0CBL             SOUND_HDA_OSD0 + 0x08 // Output Stream Descriptor Cyclic Buffer Length
#define SOUND_HDA_OSD0LVI             SOUND_HDA_OSD0 + 0x0C // Output Stream Descriptor Last Valid Index
#define SOUND_HDA_OSD0FIFOS           SOUND_HDA_OSD0 + 0x10 // Output Stream Descriptor FIFO Size
#define SOUND_HDA_OSD0FMT             SOUND_HDA_OSD0 + 0x12 // Output Stream Descriptor Format
#define SOUND_HDA_OSD0BDPL            SOUND_HDA_OSD0 + 0x18 // Output Stream Descriptor BDL Pointer Lower Base Address
#define SOUND_HDA_OSD0BDPU            SOUND_HDA_OSD0 + 0x1C // Output Stream Descriptor BDL Pointer Upper Base Address

#define SOUND_HDA_PARAM_V             0x103 // Version 1.03
#define SOUND_HDA_PARAM_NO_OUT        0x01  // Number of output streams supported
#define SOUND_HDA_PARAM_NO_IN         0x01  // Number of input streams supported
#define SOUND_HDA_PARAM_NO_BSS        0x00  // Number of bidirectional streams supported
#define SOUND_HDA_PARAM_NO_NSDO       0x00  // Number of serial data out signals
#define SOUND_HDA_PARAM_64_BIT        0x00  // 64-bit support

#define SOUND_HDA_PARAM_GCAP          ((SOUND_HDA_PARAM_NO_OUT  & 15) << 12) \
                                    | ((SOUND_HDA_PARAM_NO_IN   & 15) <<  8) \
                                    | ((SOUND_HDA_PARAM_NO_BSS  & 31) <<  3) \
                                    | ((SOUND_HDA_PARAM_NO_NSDO &  2) <<  1) \
                                    | ((SOUND_HDA_PARAM_64_BIT  &  1))

#define SOUND_HDA_PARAM_CORBSZCAP     1 /*   8 bytes =  2 entries */ \
                                    | 2 /*  64 bytes = 16 entries */ \
                                    | 4 /* 256 bytes = 32 entries */ \
                                    | 8

#define SOUND_HDA_PARAM_RIRBSZCAP     1 /*   16 bytes =   2 entries */ \
                                    | 2 /*  128 bytes =  16 entries */ \
                                    | 4 /* 2048 bytes = 256 entries */ \
                                    | 8

#define SOUND_HDA_PARAM_CORBSIZE      2 /*  256 bytes =  32 entries */
#define SOUND_HDA_PARAM_RIRBSIZE      2 /* 2048 bytes = 256 entries */

// Parameters are described in 7.3.4 Parameters
#define VERB_GET_PARAMETER                               0xF00
#define VERB_GET_CONN_SELECT_CONTROL                     0xF01
#define VERB_SET_CONN_SELECT_CONTROL                     0x701
#define VERB_GET_CONN_LIST_ENTRY                         0xF02
#define VERB_GET_PROCESSING_STATE                        0xF03
#define VERB_SET_PROCESSING_STATE                        0x703
#define VERB_GET_COEFF_INDEX                             0xD
#define VERB_SET_COEFF_INDEX                             0x5
#define VERB_GET_PROCESSING_COEFF                        0xC
#define VERB_SET_PROCESSING_COEFF                        0x4

#define VERB_GET_AMP_GAIN_MUTE                           0xB
#define VERB_GET_AMP_GAIN_MUTE_INPUT                     0x0000
#define VERB_GET_AMP_GAIN_MUTE_OUTPUT                    0x8000
#define VERB_GET_AMP_GAIN_MUTE_RIGHT                     0x0000
#define VERB_GET_AMP_GAIN_MUTE_LEFT                      0x2000

#define VERB_SET_AMP_GAIN_MUTE                           0x3
#define VERB_SET_AMP_GAIN_MUTE_MUTE                      0x80
#define VERB_SET_AMP_GAIN_MUTE_GAIN_MASK                 0x7
#define VERB_SET_AMP_GAIN_MUTE_OUTPUT                    0x8000
#define VERB_SET_AMP_GAIN_MUTE_INPUT                     0x4000
#define VERB_SET_AMP_GAIN_MUTE_LEFT                      0x2000
#define VERB_SET_AMP_GAIN_MUTE_RIGHT                     0x1000

#define VERB_GET_CONV_FMT                                0xA
#define VERB_SET_CONV_FMT                                0x2
#define VERB_GET_DIGITAL_CONV_FMT1                       0xF0D
#define VERB_GET_DIGITAL_CONV_FMT2                       0xF0E
#define VERB_SET_DIGITAL_CONV_FMT1                       0x70D
#define VERB_SET_DIGITAL_CONV_FMT2                       0x70E
#define VERB_GET_POWER_STATE                             0xF05
#define VERB_SET_POWER_STATE                             0x705
#define VERB_GET_CONV_STREAM_CHAN                        0xF06
#define VERB_SET_CONV_STREAM_CHAN                        0x706
#define VERB_GET_INPUT_CONVERTER_SDI_SELECT              0xF04
#define VERB_SET_INPUT_CONVERTER_SDI_SELECT              0x704

#define VERB_GET_PIN_WIDGET_CTRL                         0xF07
#define VERB_GET_PIN_WIDGET_CTRL_HPHN_ENABLE             (1 << 7)
#define VERB_GET_PIN_WIDGET_CTRL_OUT_ENABLE              (1 << 6)
#define VERB_GET_PIN_WIDGET_CTRL_IN_ENABLE               (1 << 5)
#define VERB_GET_PIN_WIDGET_CTRL_VREF_ENABLE             (1 << 0)

#define VERB_SET_PIN_WIDGET_CTRL                         0x707
#define VERB_GET_UNSOLICITED_RESPONSE                    0xF08
#define VERB_SET_UNSOLICITED_RESPONSE                    0x708

#define VERB_GET_PIN_SENSE                               0xF09
#define VERB_GET_PIN_SENSE_PRESENSE_PLUGGED              (1 << 31)

#define VERB_SET_PIN_SENSE                               0x709
#define VERB_GET_EAPD_BTL_ENABLE                         0xF0C
#define VERB_SET_EAPD_BTL_ENABLE                         0x70C
#define VERB_GET_GPI_DATA                                0xF10
#define VERB_SET_GPI_DATA                                0x710
#define VERB_GET_GPI_WAKE_ENABLE_MASK                    0xF11
#define VERB_SET_GPI_WAKE_ENABLE_MASK                    0x711
#define VERB_GET_GPI_UNSOLICITED_ENABLE_MASK             0xF12
#define VERB_SET_GPI_UNSOLICITED_ENABLE_MASK             0x712
#define VERB_GET_GPI_STICKY_MASK                         0xF13
#define VERB_SET_GPI_STICKY_MASK                         0x713
#define VERB_GET_GPO_DATA                                0xF14
#define VERB_SET_GPO_DATA                                0x714
#define VERB_GET_GPIO_DATA                               0xF15
#define VERB_SET_GPIO_DATA                               0x715
#define VERB_GET_GPIO_ENABLE_MASK                        0xF16
#define VERB_SET_GPIO_ENABLE_MASK                        0x716
#define VERB_GET_GPIO_DIRECTION                          0xF17
#define VERB_SET_GPIO_DIRECTION                          0x717
#define VERB_GET_GPIO_WAKE_ENABLE_MASK                   0xF18
#define VERB_SET_GPIO_WAKE_ENABLE_MASK                   0x718
#define VERB_GET_GPIO_UNSOLICITED_ENABLE_MASK            0xF19
#define VERB_SET_GPIO_UNSOLICITED_ENABLE_MASK            0x719
#define VERB_GET_GPIO_STICKY_MASK                        0xF1A
#define VERB_SET_GPIO_STICKY_MASK                        0x71A
#define VERB_GET_BEEP_GENERATION                         0xF0A
#define VERB_SET_BEEP_GENERATION                         0x70A
#define VERB_GET_VOLUME_KNOB                             0xF0F
#define VERB_SET_VOLUME_KNOB                             0x70F
#define VERB_GET_SUBSYSTEM_ID                            0xF20
#define VERB_SET_SUSBYSTEM_ID1                           0x720
#define VERB_SET_SUBSYSTEM_ID2                           0x721
#define VERB_SET_SUBSYSTEM_ID3                           0x722
#define VERB_SET_SUBSYSTEM_ID4                           0x723

#define VERB_GET_CONFIG_DEFAULT                          0xF1C
#define VERB_GET_CONFIG_DEFAULT_ASSOCIATION_DEFAULT      ( 1 <<  4)
#define VERB_GET_CONFIG_DEFAULT_COLOR_UNKNOWN            ( 0 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_BLACK              ( 1 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_GREY               ( 2 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_BLUE               ( 3 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_GREEN              ( 4 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_RED                ( 5 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_ORANGE             ( 6 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_YELLOW             ( 7 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_PURPLE             ( 8 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_PINK               ( 9 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_WHITE              (14 << 12)
#define VERB_GET_CONFIG_DEFAULT_COLOR_OTHER              (15 << 12)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_LINE_OUT          ( 0 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_SPEAKER           ( 1 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_HP_OUT            ( 2 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_CD                ( 3 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_SPDIF_OUT         ( 4 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_DIGITAL_OTHER_OUT ( 5 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_MODEM_LINE        ( 6 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_MODEM_HANDSET     ( 7 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_LIVE_IN           ( 8 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_AUX               ( 9 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_MIC_IN            (10 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_TELEPHONY         (11 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_SPDIF_IN          (12 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_DIGITAL_OTHER_IN  (13 << 20)
#define VERB_GET_CONFIG_DEFAULT_DEVICE_OTHER             (15 << 20)
#define VERB_GET_CONFIG_DEFAULT_CONNECTIVITY_JACK        ( 0 << 30)
#define VERB_GET_CONFIG_DEFAULT_CONNECTIVITY_NONE        ( 1 << 30)
#define VERB_GET_CONFIG_DEFAULT_CONNECTIVITY_FIXED       ( 2 << 30)
#define VERB_GET_CONFIG_DEFAULT_CONNECTIVITY_BOTH        ( 3 << 30)

#define VERB_SET_CONFIG_DEFAULT1                         0x71C
#define VERB_SET_CONFIG_DEFAULT2                         0x71D
#define VERB_SET_CONFIG_DEFAULT3                         0x71E
#define VERB_SET_CONFIG_DEFAULT4                         0x71F
#define VERB_GET_STRIPE_CONTROL                          0xF24
#define VERB_SET_STRIPE_CONTROL                          0x724
#define VERB_GET_CONV_CHAN_COUNT                         0xF2D
#define VERB_SET_CONV_CHAN_COUNT                         0x72D
#define VERB_FUNCTION_RESET                              0x7FF

// Applicable for VERB_GET_PARAMETER. We should assign static
// values to these parameter to emulate codec.
#define CODEC_PARAM_VENDOR_ID                            0x00
#define CODEC_PARAM_REVISION_ID                          0x02
#define CODEC_PARAM_SUB_NODE_COUNT                       0x04

#define CODEC_PARAM_FUNC_GROUP_TYPE                      0x05
#define CODEC_PARAM_FUNC_GROUP_TYPE_AUDIO                0x01
#define CODEC_PARAM_FUNC_GROUP_TYPE_MODEM                0x02

#define CODEC_PARAM_AUDIO_FUNC_GROUP_TYPE                0x08

#define CODEC_PARAM_AUDIO_WIDGET_CAPS                    0x09
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_STEREO             (1 <<  0)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_AMP_IN             (1 <<  1)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_AMP_OUT            (1 <<  2)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_AMP_OVR            (1 <<  3)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_FORMAT_OVR         (1 <<  4)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_CONN_LIST          (1 <<  8)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_OUTPUT             (0 << 20)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_INPUT              (1 << 20)
#define CODEC_PARAM_AUDIO_WIDGET_CAPS_PIN                (4 << 20)

#define CODEC_PARAM_SUPP_PCM_SIZE_RATES                  0x0A

#define CODEC_PARAM_SUPP_STREAM_FMTS                     0x0B
#define CODEC_PARAM_SUPP_STREAM_FMTS_PCM                 (1 << 0)
#define CODEC_PARAM_SUPP_STREAM_FMTS_FLOAT32             (1 << 1)
#define CODEC_PARAM_SUPP_STREAM_FMTS_AC3                 (1 << 2)

#define CODEC_PARAM_PIN_CAPS                             0x0C
#define CODEC_PARAM_PIN_CAPS_IMP_SENSE                   (1 <<  0)
#define CODEC_PARAM_PIN_CAPS_TRIGGER_REQD                (1 <<  1)
#define CODEC_PARAM_PIN_CAPS_PRESENSE_DETECT             (1 <<  2)
#define CODEC_PARAM_PIN_CAPS_HEADPHONE                   (1 <<  3)
#define CODEC_PARAM_PIN_CAPS_OUTPUT                      (1 <<  4)
#define CODEC_PARAM_PIN_CAPS_INPUT                       (1 <<  5)
#define CODEC_PARAM_PIN_CAPS_BALANCED_IO                 (1 <<  6)
#define CODEC_PARAM_PIN_CAPS_HDMI                        (1 <<  7)
#define CODEC_PARAM_PIN_CAPS_VREF_CTRL_HIZ               (1 <<  8)
#define CODEC_PARAM_PIN_CAPS_VREF_CTRL_50                (1 <<  9)
#define CODEC_PARAM_PIN_CAPS_VREF_CTRL_GROUND            (1 << 10)
#define CODEC_PARAM_PIN_CAPS_VREF_CTRL_80                (1 << 12)
#define CODEC_PARAM_PIN_CAPS_VREF_CTRL_100               (1 << 13)

#define CODEC_PARAM_INPUT_AMP_CAPS                       0x0D

#define CODEC_PARAM_OUTPUT_AMP_CAPS                      0x12
#define CODEC_PARAM_OUTPUT_AMP_CAPS_MUTE_CAP             (   1 << 31)
#define CODEC_PARAM_OUTPUT_AMP_CAPS_STEPSIZE             (   3 << 16)
#define CODEC_PARAM_OUTPUT_AMP_CAPS_NUMSTEPS             (0x4a <<  8)
#define CODEC_PARAM_OUTPUT_AMP_CAPS_OFFSET               (0x4a <<  0) // Why 0x4a?

#define CODEC_PARAM_CONN_LIST_LEN                        0x0E
#define CODEC_PARAM_SUPP_POWER_STATES                    0x0F
#define CODEC_PARAM_PROCESSING_CAPS                      0x10
#define CODEC_PARAM_GPIO_CNT                             0x11
#define CODEC_PARAM_VOLUME_KNOB                          0x12

typedef struct {
    uint8_t     bdl_lvi;
    uint32_t    bdl_lo;
    uint32_t    bdl_hi;
    uint32_t    bdl_len;
    uint32_t    lpib;
    uint8_t     ioce;
    uint8_t     stream;
    uint8_t     channel;
    uint32_t    running;
    uint8_t     fmt;
    uint8_t     status;
    uint8_t     left_gain;
    uint8_t     right_gain;
    uint8_t     left_mute;
    uint8_t     right_mute;
} sound_hda_stream_t;

typedef struct {
    pci_func_t* pci_func;
    spinlock_t  lock;
    uint32_t    gctl;
    uint32_t    intr_ctrl;
    uint32_t    corb_lo;
    uint32_t    corb_hi;
    uint16_t    corb_rp;
    uint16_t    corb_wp;
    uint32_t    corb_size;
    uint32_t    rirb_lo;
    uint32_t    rirb_hi;
    uint32_t    rirb_rp;
    uint32_t    rirb_wp;
    uint32_t    rirb_size;
    uint32_t    rirb_cnt;
    uint32_t    rirb_status;
    uint32_t    power_state;

    sound_hda_stream_t stream_output;
    sound_subsystem_t subsystem;
} sound_hda_dev_t;

static void sound_hda_remove(rvvm_mmio_dev_t* dev)
{
    // Halt the stream worker(s) before the PCI function is torn down.
    // Without this, a worker still walking the BDL will call
    // pci_send_irq() / pci_get_dma_ptr() on a freed pci_func and crash.
    //
    // Setting running=0 asks the worker to exit at its next BDL-entry
    // boundary (typically within a few ms for a 128-frame period).
    // Ideally we'd also thread_join on the worker, but RVVM's
    // thread_create_task fire-and-forgets, so we settle for a short
    // spin-wait: give the worker time to observe the flag.
    sound_hda_dev_t *hda = dev->data;
    if (hda != NULL) {
        atomic_store_uint32_relax(&hda->stream_output.running, 0);
        // The HDA stream period is 128 frames at 192 kHz = ~667 µs wall.
        // 20 ms of sleep is 30x that — comfortably long enough for the
        // worker to finish its current iteration.
        sleep_ms(20);
    }
}

static rvvm_mmio_type_t sound_hda_type = {
    .name = "intel_hda",
    .remove = sound_hda_remove,
};

static bool sound_hda_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    UNUSED(size);

    sound_hda_dev_t *hda = dev->data;
    spin_lock(&hda->lock);

    switch (offset) {
        case SOUND_HDA_GCAP:
            write_uint16_le(data, SOUND_HDA_PARAM_GCAP);
            break;
        case SOUND_HDA_VS:
            write_uint16_le(data, SOUND_HDA_PARAM_V);
            break;
        case SOUND_HDA_OUTPAY:
            write_uint16_le(data, 0x3C);
            break;
        case SOUND_HDA_INPAY:
            write_uint16_le(data, 0x1D);
            break;
        case SOUND_HDA_GLOBAL_CTRL:
            hda->gctl |= (1 << 8) | 1; // 1 << 8 is UNSOL
            write_uint32_le(data, hda->gctl);
            break;
        case SOUND_HDA_INTR_CTRL:
            write_uint32_le(data, hda->intr_ctrl);
            break;
        case SOUND_HDA_INTSTS:
            // As per specification and to simplify this code,
            // we assume that interrupt always occured when
            // guest reads this. Value represent interrupt
            // on the outptut stream.
            write_uint32_le(data, 1 << 29);
            break;
        case SOUND_HDA_CORB_WP:
            write_uint16_le(data, hda->corb_wp);
            break;
        case SOUND_HDA_CORB_RP:
            write_uint16_le(data, hda->corb_rp & 0xFF);
            break;
        case SOUND_HDA_CORB_SIZE:
            write_uint8(data, SOUND_HDA_PARAM_CORBSZCAP << 4 | SOUND_HDA_PARAM_CORBSIZE);
            break;
        case SOUND_HDA_RIRB_WP:
            write_uint16_le(data, hda->rirb_wp & 0xFF);
            break;
        case SOUND_HDA_RIRB_STATUS:
            write_uint8(data, hda->rirb_status & 0xFF);
            // Should we reset RIRB status after read?
            break;
        case SOUND_HDA_RIRB_SIZE:
            write_uint8(data, SOUND_HDA_PARAM_RIRBSZCAP << 4 | SOUND_HDA_PARAM_RIRBSIZE);
            break;
        case SOUND_HDA_OSD0STS: {
            uint8_t cmd = 0;
            if (hda->stream_output.lpib < hda->stream_output.bdl_len)
                cmd |= 1 << 5;
            else
                cmd &= ~(1 << 5);
            cmd |= hda->stream_output.status;
            write_uint8(data, cmd);
            break;
        }
        case SOUND_HDA_OSD0LPIB:
            write_uint32_le(data, hda->stream_output.lpib);
            break;
        case SOUND_HDA_OSD0FIFOS:
            write_uint16_le(data, SOUND_HDA_FIFO_SIZE);
            break;

        default:
            spin_unlock(&hda->lock);
            return false;
    }

    spin_unlock(&hda->lock);
    return true;
}

static void sound_hda_write_rirb(sound_hda_dev_t *hda, uint32_t cad, uint32_t response)
{
    ++hda->rirb_wp;
    hda->rirb_wp %= hda->rirb_size;

    uint32_t *rirb = pci_get_dma_ptr(
        hda->pci_func,
        (rvvm_addr_t) hda->rirb_lo + hda->rirb_wp * 4,
        4
    );

    uint32_t response_ex = cad;

    rirb[hda->rirb_wp] = response;
    rirb[hda->rirb_wp + 1] = response_ex;
    pci_send_irq(hda->pci_func, 0);
}

// NID = 0
static uint32_t sound_hda_codec_root_cmd(uint32_t payload)
{
    switch (payload) {
        case CODEC_PARAM_VENDOR_ID:
            return SOUND_DEVICE_ID_CMEDIA;

        case CODEC_PARAM_REVISION_ID:
            return 0xFFFF;

        case CODEC_PARAM_SUB_NODE_COUNT:
            return 0x00010001; // 1 Subnode, StartNid = 1

        default:
            return 0;
    }
}

// NID = 1
static uint32_t sound_hda_codec_fg_output_cmd(uint32_t payload)
{
    switch (payload) {
        case CODEC_PARAM_SUB_NODE_COUNT:
            return 0x00020002; // 2 Subnode, StartNid = 2

        case CODEC_PARAM_FUNC_GROUP_TYPE:
            return CODEC_PARAM_FUNC_GROUP_TYPE_AUDIO;

        case CODEC_PARAM_SUPP_PCM_SIZE_RATES:
            // Advertise ONLY 48 kHz, 16-bit. If we advertised the full
            // 8-192 kHz / 8-32 bit range, the Linux driver would let the
            // application pick its file's native rate, but the stream
            // worker has no per-stream rate awareness — it paces to a
            // fixed bytes/sec. So playing a 48 kHz WAV against a codec
            // that accepts 192 kHz caused 4× slow playback + aliasing.
            //
            // Fixing the codec to one format makes ALSA + the HDA driver
            // handle any rate mismatch in software (linear-interp upsample
            // or straight passthrough), with the emulator's rate
            // always matching the pacing math. 48 kHz mono 16-bit is
            // what every common audio file decodes to after ffmpeg/afconvert,
            // so the hot path is pure passthrough.
            return (1 << 17) | (1 << 6); // 16-bit, 48 kHz

        case CODEC_PARAM_SUPP_STREAM_FMTS:
            return CODEC_PARAM_SUPP_STREAM_FMTS_PCM;

        case CODEC_PARAM_SUPP_POWER_STATES:
            return 0xF; // D3, D2, D1, D0

        default:
            return 0;
    }
}

// NID = 2
static uint32_t sound_hda_codec_output_cmd(uint32_t payload)
{
    switch (payload) {
        case CODEC_PARAM_VENDOR_ID:
            return SOUND_DEVICE_ID_CMEDIA;

        case CODEC_PARAM_REVISION_ID:
            return 0xFFFF;

        case CODEC_PARAM_SUB_NODE_COUNT:
            return 0x00010001; // 1 Subnode, StartNid = 1

        case CODEC_PARAM_AUDIO_WIDGET_CAPS:
            // No STEREO bit: tell the guest driver this widget is mono
            // only. Prevents Linux from configuring a stereo stream
            // (which it will by default on stereo-capable widgets, even
            // for mono input, and then silently downmixes the input
            // duplicating mono → L+R — so the emulator byte rate
            // doubles and pacing diverges).
            return CODEC_PARAM_AUDIO_WIDGET_CAPS_OUTPUT
                 | CODEC_PARAM_AUDIO_WIDGET_CAPS_FORMAT_OVR
                 | CODEC_PARAM_AUDIO_WIDGET_CAPS_AMP_OVR
                 | CODEC_PARAM_AUDIO_WIDGET_CAPS_AMP_OUT;

        case CODEC_PARAM_SUPP_PCM_SIZE_RATES:
            return (1 << 17) | (1 << 6); // 16-bit, 48 kHz (see root-node comment)

        case CODEC_PARAM_SUPP_STREAM_FMTS:
            return CODEC_PARAM_SUPP_STREAM_FMTS_PCM;

        case CODEC_PARAM_OUTPUT_AMP_CAPS:
            return CODEC_PARAM_OUTPUT_AMP_CAPS_MUTE_CAP
                 | CODEC_PARAM_OUTPUT_AMP_CAPS_STEPSIZE
                 | CODEC_PARAM_OUTPUT_AMP_CAPS_NUMSTEPS
                 | CODEC_PARAM_OUTPUT_AMP_CAPS_OFFSET;

        default:
            return 0;
    }
}

// NID = 3
static uint32_t sound_hda_codec_pin_output_cmd(uint32_t payload)
{
    switch (payload) {
        case CODEC_PARAM_AUDIO_WIDGET_CAPS:
            // Mono pin to match the output widget; see the output-widget
            // caps comment above.
            return CODEC_PARAM_AUDIO_WIDGET_CAPS_PIN
                 | CODEC_PARAM_AUDIO_WIDGET_CAPS_CONN_LIST;

        case CODEC_PARAM_PIN_CAPS:
            return CODEC_PARAM_PIN_CAPS_OUTPUT
                 | CODEC_PARAM_PIN_CAPS_PRESENSE_DETECT;

        case CODEC_PARAM_CONN_LIST_LEN:
            return 1;

        default:
            return 0;
    }
}

#define NODE_ID_ROOT       0 // Root node
#define NODE_ID_FG_OUTPUT  1 // Output function group
#define NODE_ID_OUTPUT     2 // Output
#define NODE_ID_PIN_OUTPUT 3 // Pin output

static uint32_t sound_hda_codec_stream_cmd(sound_hda_stream_t *stream, uint32_t nid, uint32_t verb, uint32_t payload)
{
    uint32_t response = 0;
    uint8_t  mute = 0;
    uint8_t  gain = 0;

    switch (verb) {
        case VERB_GET_CONV_FMT:
            response = stream->fmt;
            break;
        case VERB_SET_CONV_FMT:
            stream->fmt = payload;
            break;
        case VERB_GET_AMP_GAIN_MUTE:
            if (payload & VERB_GET_AMP_GAIN_MUTE_LEFT) {
                response = stream->left_gain | stream->left_mute;
            }

            if (payload & VERB_GET_AMP_GAIN_MUTE_RIGHT) {
                response = stream->right_gain | stream->right_mute;
            }

            break;
        case VERB_SET_AMP_GAIN_MUTE:
            mute = VERB_SET_AMP_GAIN_MUTE_MUTE;
            gain = VERB_SET_AMP_GAIN_MUTE_GAIN_MASK;

            if (payload & VERB_SET_AMP_GAIN_MUTE_LEFT) {
                stream->left_mute = mute;
                stream->left_gain = gain;
            }

            if (payload & VERB_SET_AMP_GAIN_MUTE_RIGHT) {
                stream->right_mute = mute;
                stream->right_gain = gain;
            }

            break;
        case VERB_GET_CONV_STREAM_CHAN:
            response = (stream->stream << 4) | stream->channel;
            break;
        case VERB_SET_CONV_STREAM_CHAN:
            stream->channel = payload & 0x0F;
            stream->stream = (payload >> 4) & 0x0F;
            break;
        case VERB_GET_PIN_WIDGET_CTRL:
            switch (nid) {
            case NODE_ID_PIN_OUTPUT:
                response = VERB_GET_PIN_WIDGET_CTRL_OUT_ENABLE;
                break;
            }
            break;
        default:
            break;
    }

    return response;
}

#define VERB_PARAM_12BIT_SHIFT      8
#define VERB_PARAM_4BIT_SHIFT      16
#define VERB_PARAM_CAD_SHIFT       28
#define VERB_PARAM_NID_SHIFT       20

static void sound_hda_codec_cmd(sound_hda_dev_t *hda, uint32_t cmd)
{
    uint8_t cad = 0;
    uint8_t nid = 0;
    uint16_t verb = 0;
    uint16_t payload = 0;
    uint32_t response = 0;

    // 4.4.1.5 Other CORB Programming Notes
    // Zero is valid CORB command.
    if (cmd == 0)
        return;

    cad = (cmd >> VERB_PARAM_CAD_SHIFT) & 0x0F;
    nid = (cmd >> VERB_PARAM_NID_SHIFT) & 0xFF;

    if ((cmd & 0x70000) == 0x70000) {
        verb = (cmd >> VERB_PARAM_12BIT_SHIFT) & 0x0FFF;
        payload = cmd & 0xFF;
    } else {
        verb = (cmd >> VERB_PARAM_4BIT_SHIFT) & 0x0F;
        payload = cmd & 0xFFFF;
    }

    switch (verb) {
        case VERB_GET_PARAMETER:
            // We define NID count and start indices in CODEC_PARAM_SUB_NODE_COUNT
            switch (nid) {
            case NODE_ID_ROOT:
                response = sound_hda_codec_root_cmd(payload);
                break;
            case NODE_ID_FG_OUTPUT:
                response = sound_hda_codec_fg_output_cmd(payload);
                break;
            case NODE_ID_OUTPUT:
                response = sound_hda_codec_output_cmd(payload);
                break;
            case NODE_ID_PIN_OUTPUT:
                response = sound_hda_codec_pin_output_cmd(payload);
                break;
            }

            break;
        case VERB_GET_SUBSYSTEM_ID:
            response = (SOUND_DEVICE_ID_CMEDIA << 16) | 1;
            break;
        case VERB_SET_POWER_STATE:
            hda->power_state = payload;
            response = 0;
            break;
        case VERB_GET_POWER_STATE:
            //         PS-Set              PS-Act
            response = hda->power_state | (hda->power_state << 4);
            break;
        case VERB_GET_PIN_WIDGET_CTRL:
            response = VERB_GET_PIN_WIDGET_CTRL_OUT_ENABLE;
            break;
        case VERB_GET_PIN_SENSE:
            response = VERB_GET_PIN_SENSE_PRESENSE_PLUGGED;
            break;
        case VERB_GET_CONN_LIST_ENTRY:
            response = NODE_ID_OUTPUT;
            break;
        case VERB_GET_CONFIG_DEFAULT:
            response = VERB_GET_CONFIG_DEFAULT_CONNECTIVITY_JACK
                     | VERB_GET_CONFIG_DEFAULT_DEVICE_LINE_OUT
                     | VERB_GET_CONFIG_DEFAULT_ASSOCIATION_DEFAULT
                     | VERB_GET_CONFIG_DEFAULT_COLOR_ORANGE;
            break;
        default:
            switch (nid) {
            case NODE_ID_OUTPUT:
                response = sound_hda_codec_stream_cmd(&hda->stream_output, nid, verb, payload);
                break;
            }
            break;
    }

    sound_hda_write_rirb(hda, cad, response);
}

static void *sound_hda_stream_worker(void *arg)
{
    sound_hda_dev_t *hda = arg;
    sound_hda_stream_t *stream = &hda->stream_output;

    // Pace the worker to the stream's configured bytes-per-second.
    // Without pacing, non-blocking backends (ring buffers, null sinks)
    // let this loop blast through the BDL as fast as the CPU allows:
    // LPIB advances instantly, the guest HDA driver sees its DMA
    // "consume" data faster than it can refill, and aplay trips ALSA's
    // position-consistency assertion ("pcm_plugin.c Assertion
    // status->appl_ptr == *pcm->appl.ptr failed"). Blocking backends
    // (ALSA's snd_pcm_writei) dodge this accidentally because writei
    // blocks when the host buffer fills.
    //
    // The rate is derived from stream->fmt per HDA spec 7.3.3.10:
    //   bit  14      : base rate        (0=48000, 1=44100)
    //   bits 13:11   : rate multiplier  (N+1: 1×, 2×, 3×, 4×)
    //   bits 10:8    : rate divisor     (N+1: /1 .. /8)
    //   bits 6:4     : bits/sample      (0=8, 1=16, 2=20, 3=24, 4=32)
    //   bits 3:0     : channels minus 1 (0=1ch, 1=2ch, …)
    //
    // A previous iteration hardcoded 192 kHz mono then later 48 kHz
    // mono — both broke when the guest driver chose a different stream
    // format (Linux HDA likes to configure stereo streams even for mono
    // content, doubling the true byte rate). Deriving from fmt is the
    // only way to be robust across guest driver choices.
    uint16_t fmt             = stream->fmt;
    uint32_t channels        = (fmt & 0xF) + 1;
    uint32_t bits_code       = (fmt >> 4) & 7;
    uint32_t bits_per_sample = bits_code == 0 ? 8  : bits_code == 1 ? 16 :
                               bits_code == 2 ? 20 : bits_code == 3 ? 24 : 32;
    uint32_t bytes_per_frame = channels * ((bits_per_sample + 7) / 8);
    uint32_t div             = ((fmt >> 8)  & 7) + 1;
    uint32_t mult            = ((fmt >> 11) & 7) + 1;
    uint32_t base_hz         = (fmt & (1 << 14)) ? 44100 : 48000;
    uint64_t sample_rate_hz  = (uint64_t)base_hz * mult / div;
    uint64_t SAMPLE_RATE_BYTES_PER_SEC = sample_rate_hz * bytes_per_frame;
    if (SAMPLE_RATE_BYTES_PER_SEC == 0) {
        // Guest wrote run=1 before configuring the format. Bail out
        // like the NULL-dma case — driver will retry properly.
        atomic_store_uint32_relax(&stream->running, 0);
        return NULL;
    }
    uint64_t       paced_start_ns  = 0;
    uint64_t       paced_bytes_out = 0;

    uint32_t total = stream->bdl_lvi + 1;
    uint64_t *dma = pci_get_dma_ptr(hda->pci_func, stream->bdl_lo, stream->bdl_len);

    // If the guest set up the stream control register without a valid BDL
    // (bdl_lo == 0 or invalid), pci_get_dma_ptr returns NULL. Bail out
    // cleanly — the guest's ALSA driver will eventually retry with a
    // proper BDL when userspace opens another PCM handle.
    if (dma == NULL) {
        atomic_store_uint32_relax(&stream->running, 0);
        return NULL;
    }

    while (atomic_load_uint32_relax(&stream->running)) {
        for (uint32_t i = 0; i < total; ++i) {
            uint64_t *bdle = &dma[i * 2];
            uint64_t addr = bdle[0];
            uint32_t len  = bdle[1] & 0xFFFFFFFF;
            uint8_t  ioc  = bdle[1] >> 32 & 1;

            // Dispatch PCM to the configured host-side backend. If no backend
            // was installed at init time (neither a compile-time USE_ALSA nor
            // a caller-supplied write_fn via sound_hda_init_ex), the HDA
            // device enumerates on the PCI bus but this path is a no-op —
            // the guest sees a working device and the LPIB counter advances
            // so its driver doesn't stall, but PCM data is silently dropped.
            //
            // Backend contract: always receives MONO 16-bit LE at the
            // stream's configured rate. If the guest driver configured a
            // multi-channel stream (Linux's HDA code likes stereo even
            // when the codec widget advertises mono capability), we
            // downmix here by averaging channels. Keeps backends simple
            // (they don't need to know channel count) and the pacing
            // math below stays 1:1 with the bytes they see.
            if (hda->subsystem.write != NULL) {
                void *pcm = pci_get_dma_ptr(hda->pci_func, addr, len);
                if (channels == 1 || pcm == NULL) {
                    hda->subsystem.write(&hda->subsystem, pcm, len);
                } else if (bits_per_sample == 16) {
                    // Common case: 16-bit multi-channel → 16-bit mono.
                    // Stack-allocate — BDL entries are small (256-4096 B).
                    size_t frame_bytes_in = (size_t)bytes_per_frame;
                    size_t frames         = len / frame_bytes_in;
                    int16_t *src = (int16_t*)pcm;
                    int16_t  mono_buf[4096];
                    size_t bytes_emitted = 0;
                    while (bytes_emitted < frames * 2) {
                        size_t chunk_frames = frames - (bytes_emitted / 2);
                        if (chunk_frames > 4096) chunk_frames = 4096;
                        for (size_t f = 0; f < chunk_frames; f++) {
                            int32_t sum = 0;
                            size_t base = (bytes_emitted / 2 + f) * channels;
                            for (uint32_t c = 0; c < channels; c++) {
                                sum += src[base + c];
                            }
                            mono_buf[f] = (int16_t)(sum / (int32_t)channels);
                        }
                        hda->subsystem.write(&hda->subsystem, mono_buf, chunk_frames * 2);
                        bytes_emitted += chunk_frames * 2;
                    }
                } else {
                    // Rare: non-16-bit multi-channel. Pass raw; backends
                    // that care can parse stream->fmt from the caller.
                    hda->subsystem.write(&hda->subsystem, pcm, len);
                }
            } else {
                UNUSED(addr);
            }

            // Wall-clock pacing. Compute the ideal elapsed time for the
            // bytes we've emitted so far and sleep the difference.
            paced_bytes_out += len;
            uint64_t now_ns = rvtimer_clocksource(1000000000ULL);
            if (paced_start_ns == 0) {
                paced_start_ns = now_ns;
            } else {
                uint64_t expected_ns = paced_bytes_out * 1000000000ULL
                                     / SAMPLE_RATE_BYTES_PER_SEC;
                uint64_t elapsed_ns  = now_ns - paced_start_ns;
                if (expected_ns > elapsed_ns) {
                    sleep_ns(expected_ns - elapsed_ns);
                } else if (elapsed_ns > expected_ns + 100000000ULL) {
                    // Fell more than 100 ms behind — rebase so we don't
                    // try to "catch up" by blasting. Happens on machine
                    // resume from pause, or very long Java-side GCs.
                    paced_start_ns  = now_ns;
                    paced_bytes_out = 0;
                }
            }

            stream->lpib += len;
            // If stream longer than BDL length, reset LPIB.
            if (stream->lpib >= stream->bdl_len)
                stream->lpib = 0;
            // When LPIB goes over BDL length, we are done.
            if (stream->bdl_len > 0 && stream->lpib > stream->bdl_len)
                atomic_store_uint32_relax(&stream->running, 0);
            if (ioc)
                pci_send_irq(hda->pci_func, 0);
        }
    }

    return NULL;
}

static void sound_hda_output_stream_ctl(sound_hda_dev_t *hda, uint32_t cmd)
{
    uint8_t ioce = (cmd >> 2) & 1;
    uint8_t run  = (cmd >> 1) & 1;

    sound_hda_stream_t *stream = &hda->stream_output;
    stream->ioce = ioce;

    if (run) {
        // Only spawn a worker if one isn't already running. Without this
        // guard, a guest that writes run=1 twice in quick succession (or
        // a backend slow enough that one aplay ends before the worker
        // exits and the next aplay starts) can leave two workers walking
        // the same stream struct. They'd race on stream->lpib / running,
        // and if one worker's cached dma pointer got stale from a guest
        // BDL update in between, dereferencing it crashes.
        if (atomic_cas_uint32(&stream->running, 0, 1)) {
            thread_create_task(sound_hda_stream_worker, hda);
        }
    } else {
        atomic_store_uint32_relax(&stream->running, 0);
    }
}

static bool sound_hda_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    UNUSED(size);

    sound_hda_dev_t *hda = dev->data;
    spin_lock(&hda->lock);

    switch (offset) {
        case SOUND_HDA_GLOBAL_CTRL:
            hda->gctl = read_uint32_le(data);
            break;
        case SOUND_HDA_INTR_CTRL:
            hda->intr_ctrl = read_uint32_le(data);
            break;
        case SOUND_HDA_CORB_LO:
            hda->corb_lo = read_uint32_le(data);
            break;
        case SOUND_HDA_CORB_HI:
            hda->corb_hi = read_uint32_le(data);
            break;
        case SOUND_HDA_CORB_WP: {
            hda->corb_wp = read_uint16_le(data) & 0x7F;
            uint32_t *cmd = pci_get_dma_ptr(
                hda->pci_func,
                (rvvm_addr_t) hda->corb_lo + hda->corb_wp * 4,
                4
            );
            sound_hda_codec_cmd(hda, *cmd);
            break;
        }
        case SOUND_HDA_CORB_RP: {
            uint16_t cmd = read_uint16_le(data);
            if (cmd & 0x8000)
                hda->corb_rp = 0;
            else
                hda->corb_rp = cmd & 0x7F;
            break;
        }
        case SOUND_HDA_CORB_SIZE: {
            uint8_t cmd = read_uint8(data);
            switch (cmd & 3) {
            case 0: hda->corb_size =    8; break;
            case 1: hda->corb_size =   64; break;
            case 2: hda->corb_size = 1024; break;
            case 3: /* Reserved */         break;
            }
            break;
        }
        case SOUND_HDA_RIRB_LO:
            hda->rirb_lo = read_uint32_le(data);
            break;
        case SOUND_HDA_RIRB_HI:
            hda->rirb_hi = read_uint32_le(data);
            break;
        case SOUND_HDA_RIRB_WP: {
            uint16_t cmd = read_uint16_le(data);
            if (cmd & 0x8000)
                hda->rirb_wp = 0;
            else
                hda->rirb_wp = cmd & 0xFF;
            break;
        }
        case SOUND_HDA_RIRB_INTR_CNT:
            hda->rirb_cnt = read_uint16_le(data);
            break;
        case SOUND_HDA_RIRB_STATUS: {
            uint8_t cmd = read_uint8(data);

            if (cmd & 0x01)
                hda->rirb_status &= ~0x1;

            if (cmd & 0x02)
                hda->rirb_status &= ~0x2;

            break;
        }
        case SOUND_HDA_RIRB_SIZE: {
            uint8_t cmd = read_uint8(data);
            switch (cmd & 3) {
            case 0: hda->rirb_size =   16; break;
            case 1: hda->rirb_size =  128; break;
            case 2: hda->rirb_size = 2048; break;
            case 3: /* Reserved */         break;
            }
            break;
        }
        case SOUND_HDA_OSD0CTL:
            sound_hda_output_stream_ctl(hda, read_uint32_le(data));
            break;
        case SOUND_HDA_OSD0STS: {
            uint8_t cmd = read_uint8(data);

            uint8_t bcis  = (cmd >> 2) & 1;
            uint8_t fifoe = (cmd >> 3) & 1;
            uint8_t dese  = (cmd >> 4) & 1;

            if (bcis)
                hda->stream_output.status &= ~0x04;

            if (fifoe)
                hda->stream_output.status &= ~0x08;

            if (dese)
                hda->stream_output.status &= ~0x10;

            break;
        }
        case SOUND_HDA_OSD0CBL:
            hda->stream_output.bdl_len = read_uint32_le(data);
            break;
        case SOUND_HDA_OSD0LVI:
            hda->stream_output.bdl_lvi = read_uint32_le(data);
            break;
        case SOUND_HDA_OSD0FMT:
            // Maybe useful, but guest already plays sound
            // with different sample rates correctly.
            break;
        case SOUND_HDA_OSD0BDPL:
            hda->stream_output.bdl_lo = read_uint32_le(data);
            break;
        case SOUND_HDA_OSD0BDPU:
            hda->stream_output.bdl_hi = read_uint32_le(data);
            break;
        default:
            spin_unlock(&hda->lock);
            return false;
    }

    spin_unlock(&hda->lock);
    return true;
}

/*
 * Bridge struct stored in sound_subsystem_t.sound_data when a caller-supplied
 * backend is installed via sound_hda_init_ex. Adapts the public two-argument
 * callback to the subsystem's three-argument shape. Freed... never. The HDA
 * device itself is leaked on machine destruction today (see sound_hda_remove),
 * so this follows the same lifecycle.
 */
typedef struct {
    sound_hda_backend_write_fn user_fn;
    void                       *user_data;
} sound_hda_backend_bridge_t;

static void sound_hda_backend_bridge_write(sound_subsystem_t *sub, void *data, size_t size)
{
    sound_hda_backend_bridge_t *bridge = (sound_hda_backend_bridge_t *)sub->sound_data;
    if (bridge != NULL && bridge->user_fn != NULL) {
        bridge->user_fn(bridge->user_data, data, size);
    }
}

PUBLIC pci_dev_t *sound_hda_init_ex(pci_bus_t *pci_bus,
                                    sound_hda_backend_write_fn write_fn,
                                    void *user_data)
{
    sound_hda_dev_t *sound_hda = safe_new_obj(sound_hda_dev_t);

    pci_func_desc_t sound_hda_desc = {
        .vendor_id  = SOUND_VENDOR_ID_CMEDIA,
        .device_id  = SOUND_DEVICE_ID_CMEDIA,
        .class_code = SOUND_CLASS_CODE_CMEDIA,
        .prog_if    = 0x00,
        .irq_pin    = PCI_IRQ_PIN_INTA,
        .bar[0] = {
            .size        = 0x4000,
            .min_op_size = 1,
            .max_op_size = 4,
            .read        = sound_hda_mmio_read,
            .write       = sound_hda_mmio_write,
            .data        = sound_hda,
            .type        = &sound_hda_type
        },
    };

    pci_dev_t *pci_dev = pci_attach_func(pci_bus, &sound_hda_desc);
    if (pci_dev)
        sound_hda->pci_func = pci_get_device_func(pci_dev, 0);

    // Backend selection priority:
    //   1. Caller-supplied write_fn (via sound_hda_init_ex) — skip the
    //      compile-time default. Used by embedders that want to route
    //      audio somewhere other than the host's native audio stack
    //      (managed runtimes, IPC channels, WAV capture fixtures, etc.).
    //   2. Compile-time USE_ALSA — the traditional Linux host path.
    //   3. Neither — PCI device enumerates but the stream worker drops PCM.
    if (write_fn != NULL) {
        sound_hda_backend_bridge_t *bridge = safe_new_obj(sound_hda_backend_bridge_t);
        bridge->user_fn = write_fn;
        bridge->user_data = user_data;
        sound_hda->subsystem.sound_data = bridge;
        sound_hda->subsystem.write = sound_hda_backend_bridge_write;
    } else {
#ifdef USE_ALSA
        if (!alsa_sound_init(&sound_hda->subsystem))
            return NULL;
#endif
    }

    return pci_dev;
}

PUBLIC pci_dev_t *sound_hda_init_auto_ex(rvvm_machine_t *machine,
                                         sound_hda_backend_write_fn write_fn,
                                         void *user_data)
{
    return sound_hda_init_ex(rvvm_get_pci_bus(machine), write_fn, user_data);
}

PUBLIC pci_dev_t *sound_hda_init(pci_bus_t *pci_bus)
{
    return sound_hda_init_ex(pci_bus, NULL, NULL);
}

PUBLIC pci_dev_t *sound_hda_init_auto(rvvm_machine_t *machine)
{
    return sound_hda_init(rvvm_get_pci_bus(machine));
}

POP_OPTIMIZATION_SIZE
