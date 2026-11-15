# Framework Arduino pour Goodix GR551x.
#
#   - SDK Goodix GR551x (paquet framework-gr551x-sdk) : démarrage, pilotes app_*, pile BLE en ROM (libble_sdk) ;
#   - cœur Arduino (cores/<core>) : ArduinoCore-API + implémentation GR551x ;
#   - variante (variants/<variant>) : brochage, custom_config.h du SDK, script d'édition de liens, init. carte.
#
# Le SDK, le cœur et la variante sont compilés en fichiers objets (et non en archives) : plusieurs symboles
# faibles du SDK (gestionnaires de faute, hooks) sont remplacés par la variante, ce qui ne fonctionne pas de
# façon fiable depuis une archive.

from os.path import isdir, join

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

SDK = platform.get_package_dir("framework-gr551x-sdk")
assert SDK and isdir(SDK), "paquet framework-gr551x-sdk introuvable"
PLAT = platform.get_dir()
CORE = join(PLAT, "cores", board.get("build.core"))
VARIANT = join(PLAT, "variants", board.get("build.variant"))

CPU = ["-mthumb", "-mcpu=cortex-m4", "-mfloat-abi=softfp", "-mfpu=fpv4-sp-d16"]

SDK_INC = [
    "components/libraries/app_assert",
    "components/libraries/app_error",
    "components/libraries/app_log",
    "components/libraries/app_timer",
    "components/libraries/hal_flash",
    "components/libraries/pmu_calibration",
    "components/libraries/utility",
    "components/libraries/ring_buffer",
    "components/profiles/common",
    "components/profiles/gus",
    "components/sdk",
    "components/patch/ind",
    "drivers/inc",
    "drivers/inc/hal",
    "platform/arch/arm/cortex-m/cmsis/core/include",
    "platform/boards",
    "platform/include",
    "platform/soc/include",
    "platform/soc/linker/keil",
]

SDK_SRC = [
    "platform/soc/common/gr_system.c",
    "platform/soc/common/gr_interrupt.c",
    "platform/soc/common/gr_platform.c",
    "platform/soc/src/gr_soc.c",
    "drivers/src/app_io.c",
    "drivers/src/app_gpiote.c",
    "drivers/src/app_pwr_mgmt.c",
    "drivers/src/app_aon_wdt.c",
    "drivers/src/app_spi.c",
    "drivers/src/app_dma.c",
    "drivers/src/app_adc.c",
    "drivers/src/app_uart.c",
    "drivers/src/app_pwm.c",
    "components/libraries/ring_buffer/ring_buffer.c",
    "components/libraries/utility/utility.c",
    "components/libraries/pmu_calibration/pmu_calibration.c",
    "components/libraries/app_timer/app_timer.c",
    "components/libraries/app_assert/app_assert.c",
    "components/libraries/app_error/app_error.c",
    "components/libraries/app_log/app_log.c",
    "components/sdk/ble.c",
    "components/profiles/common/ble_prf_utils.c",
    "components/profiles/gus/gus.c",
]

env.Append(
    ASFLAGS=CPU,
    ASPPFLAGS=["-x", "assembler-with-cpp"],
    CCFLAGS=CPU + [
        "-Os", "-g",
        "-ffunction-sections", "-fdata-sections",
        "-fno-strict-aliasing",
        "-Wall", "-Wno-format", "-Wno-unused-function", "-Wno-unused-variable",
    ],
    CFLAGS=["-std=gnu11"],
    CXXFLAGS=[
        "-std=gnu++17",
        "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit",
        "-Wno-register", "-Wno-volatile", "-Wno-sign-compare",
    ],
    CPPDEFINES=[
        ("ARDUINO", 10819),
        "ARDUINO_ARCH_GR551X",
        ("ARDUINO_" + board.get("build.variant").upper().replace("-", "_")),
        ("F_CPU", "$BOARD_F_CPU"),
        "GR551X",
    ],
    CPPPATH=[
        CORE,
        join(CORE, "api", "deprecated"),
        join(CORE, "api", "deprecated-avr-comp"),
        VARIANT,
    ] + [join(SDK, d) for d in SDK_INC],
    LINKFLAGS=CPU + [
        "-specs=nosys.specs",
        "-Wl,--gc-sections",
        "-Wl,--no-warn-rwx-segments",
        "-Wl,-Map=" + join("$BUILD_DIR", "${PROGNAME}.map"),
        join(SDK, "platform", "soc", "linker", "gcc", "rom_symbol_gcc.txt"),
    ],
    LIBPATH=[
        VARIANT,
        join(SDK, "platform", "soc", "linker", "gcc"),
        join(SDK, "components", "libraries", "app_crypto", "gcc"),
    ],
    LIBS=["app_crypto", "ble_sdk", "stdc++", "m", "c", "gcc", "nosys"],
)

env.Replace(LDSCRIPT_PATH=join(VARIANT, board.get("build.ldscript")))
env.Depends("$BUILD_DIR/${PROGNAME}.elf", "$LDSCRIPT_PATH")      # relier à nouveau si le script change

env.Prepend(_LIBFLAGS="-Wl,--start-group ")
env.Append(_LIBFLAGS=" -Wl,--end-group")

# Sources : SDK (liste explicite), cœur Arduino, variante.
env.BuildSources(join("$BUILD_DIR", "FrameworkSDK"), SDK, src_filter=["-<*>"] + ["+<%s>" % s for s in SDK_SRC])
env.BuildSources(join("$BUILD_DIR", "FrameworkArduino"), CORE)
env.BuildSources(join("$BUILD_DIR", "FrameworkArduinoVariant"), VARIANT)

# Code de démarrage EN DERNIER : il déclare chaque gestionnaire d'exception comme alias faible de
# Default_Handler (boucle infinie), et gr_interrupt.c définit les vrais gestionnaires… eux aussi en faible.
# Entre deux définitions faibles, l'éditeur de liens garde la première rencontrée : si le démarrage passe
# avant gr_interrupt.o, SVC_Handler reste une boucle infinie et la pile BLE (correctifs ROM appliqués par
# FPB + SVC) se bloque dans ble_stack_init(). Voir aussi tools/check_handlers.py.
env.Append(PIOBUILDFILES=env.Object(
    join("$BUILD_DIR", "FrameworkSDK", "startup_gr55xx.o"),
    join(SDK, "platform", "arch", "arm", "cortex-m", "gcc", "startup_gr55xx.s"),
))

# Bibliothèques fournies avec la plateforme (SPI, …)
env.Append(LIBSOURCE_DIRS=[join(PLAT, "libraries")])
