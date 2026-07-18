cmake_minimum_required(VERSION 3.13)
project(RVVM C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ==========================================
# НАШИ ИЗМЕНЕНИЯ: Поиск аудио-бэкенда ALSA на хосте
# ==========================================
find_package(ALSA REQUIRED)

# Базовые флаги компиляции
add_compile_options(-Wall -Wextra -O3)

# Папки с заголовочными файлами
include_directories(include src)

# Список исходных файлов ядра эмулятора (librvvm)
set(LIBRVVM_SOURCES
    src/machine/machine.c
    src/machine/mmu.c
    src/machine/pci.c
    src/cpu/riscv.c
    src/cpu/interpreter.c
    src/devices/uart.c
    src/devices/nvme.c
    src/devices/network.c
    # ==========================================
    # НАШИ ИЗМЕНЕНИЯ: Добавляем аудиокарту в сборку
    # ==========================================
    src/devices/dsd_sound.c
    src/utils/log.h
    src/utils/log.c
)

# Собираем основную библиотеку эмулятора
add_library(rvvm_core STATIC ${LIBRVVM_SOURCES})

# Главный исполняемый файл RVVM
add_executable(rvvm src/rvvm.c)

# Линковка зависимостей
# ==========================================
# НАШИ ИЗМЕНЕНИЯ: Привязываем ядро к исполняемому файлу и добавляем ALSA
# ==========================================
target_link_libraries(rvvm PRIVATE rvvm_core ALSA::ALSA)

# Установка (опционально)
install(TARGETS rvvm DESTINATION bin)