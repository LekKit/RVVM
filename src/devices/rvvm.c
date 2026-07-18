#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rvvm.h"
#include "utils/log.h"

// Объявляем конструктор нашего DSD устройства
void rvvm_create_dsd_sound(rvvm_machine_t *machine);

int main(int argc, char** argv) {
    rvvm_log_info("Initializing RVVM - The RISC-V Virtual Machine...");

    // 1. Создание конфигурации машины
    rvvm_config_t config;
    memset(&config, 0, sizeof(config));
    
    if (!rvvm_parse_args(&config, argc, argv)) {
        return 1;
    }

    // 2. Сборка самой виртуальной машины (процессор, память, шина PCI)
    rvvm_machine_t *machine = rvvm_create_machine(&config);
    if (!machine) {
        rvvm_log_error("Failed to create RISC-V machine instance");
        return 1;
    }

    // ==========================================
    // НАШИ ИЗМЕНЕНИЯ: "Вставляем" аудиокарту в PCI-слот
    // ==========================================
    if (machine->pci_bus) {
        rvvm_create_dsd_sound(machine);
    } else {
        rvvm_log_warn("DSD Audio: PCI bus not found, skipping device initialization");
    }
    // ==========================================

    // 3. Запуск виртуальной машины
    rvvm_log_info("Starting guest OS execution...");
    bool success = rvvm_run_machine(machine);

    // 4. Очистка ресурсов после завершения работы VM
    rvvm_free_machine(machine);
    
    return success ? 0 : 1;
}