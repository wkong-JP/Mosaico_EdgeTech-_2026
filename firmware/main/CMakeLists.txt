# The portable core is shared with the Host simulator: app/mosaico_experience/main/exp_*.c
set(EXP_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../app/mosaico_experience/main")
file(GLOB EXP_CORE_SRCS "${EXP_CORE_DIR}/exp_*.c")

# EXP_VOICE comes from the top-level CMakeLists (as a cache option and as an environment variable: this file is also
# evaluated in a separate process that only inherits the environment).
set(EXP_VOICE_ON OFF)
if(EXP_VOICE OR "$ENV{EXP_VOICE}" STREQUAL "1")
    set(EXP_VOICE_ON ON)
endif()
set(EXP_VOICE_REQ "")
if(EXP_VOICE_ON)
    set(EXP_VOICE_REQ exp_voice)
endif()

# Camera preview (stage 4a): same mechanism, set by the top-level CMakeLists with -DEXP_VISION=1.
set(EXP_VISION_ON OFF)
if(EXP_VISION OR "$ENV{EXP_VISION}" STREQUAL "1")
    set(EXP_VISION_ON ON)
endif()
set(EXP_VISION_REQ "")
if(EXP_VISION_ON)
    set(EXP_VISION_REQ exp_vision)
endif()

idf_component_register(
    SRCS "device_main.c" "dev_sensors.c" "dev_audio.c" "dev_slots.c" "dev_joystick.c" "dev_watchdog.c" "dev_rf_probe.c" "dev_magnet.c" "assets/m5stack_logo.c" ${EXP_CORE_SRCS}
    INCLUDE_DIRS "." "assets" "${EXP_CORE_DIR}"
    REQUIRES exp_surface exp_dbglog
    PRIV_REQUIRES esp-mosaico-bsp mosaico_module_mgr mosaico_module_joystick espressif__esp_lcd_touch esp_lcd esp_timer esp_driver_gpio esp_system freertos heap log esp_iris ${EXP_VOICE_REQ} ${EXP_VISION_REQ})


# Exhibition profile: idf.py -DEXP_RELEASE=1 build   (no log view / timing overlay / boot benchmark; version string ends in -rel)
option(EXP_RELEASE "Build the exhibition profile (debug aids removed)" OFF)
if(EXP_RELEASE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EXP_RELEASE=1 EXP_DBGLOG=0 EXP_TIMING_OVERLAY=0 EXP_BOOT_BENCH=0)
endif()

# Offline speech control (set by the top-level CMakeLists with -DEXP_VOICE=1).
if(EXP_VOICE_ON)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EXP_VOICE=1)
endif()

# Camera preview (set by the top-level CMakeLists with -DEXP_VISION=1).
if(EXP_VISION_ON)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EXP_VISION=1)
endif()
