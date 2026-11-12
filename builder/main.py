# Construction pour Goodix GR551x : ELF -> binaire brut -> image DFU (structure Goodix de 48 octets en fin).
#
# Téléversement « ble-dfu » : bootloader d'origine du SenseCAP T1000-A, par Bluetooth depuis le PC
# (tools/t1000_ble.py, nécessite le paquet Python « bleak »).

from os.path import join

from SCons.Script import AlwaysBuild, Builder, Default, DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

TOOLS = join(platform.get_dir(), "tools")

env.Replace(
    AR="arm-none-eabi-gcc-ar",
    AS="arm-none-eabi-as",
    CC="arm-none-eabi-gcc",
    CXX="arm-none-eabi-g++",
    GDB="arm-none-eabi-gdb",
    LINK="arm-none-eabi-g++",
    OBJCOPY="arm-none-eabi-objcopy",
    RANLIB="arm-none-eabi-gcc-ranlib",
    SIZETOOL="arm-none-eabi-size",
    ARFLAGS=["rc"],
    SIZEPROGREGEXP=r"^(?:\.text|\.data|\.rodata|\.ARM\.exidx|\.ARM\.extab)\s+(\d+).*",
    SIZEDATAREGEXP=r"^(?:\.data|\.bss|\.heap)\s+(\d+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",
    PROGSUFFIX=".elf",
)

env.Append(
    BUILDERS=dict(
        ElfToBin=Builder(
            action=env.VerboseAction("$OBJCOPY -O binary $SOURCES $TARGET", "Building $TARGET"),
            suffix=".bin",
        ),
        DfuImage=Builder(
            action=env.VerboseAction(
                '"$PYTHONEXE" -I "%s" "${SOURCES[0]}" "$TARGET" --load %s --limit %s --comment "%s" '
                '--check-elf "${SOURCES[1]}"'
                % (
                    join(TOOLS, "mkimage.py"),
                    board.get("build.app_load"),
                    board.get("build.app_limit"),
                    board.get("build.image_comment", "GR551x"),
                ),
                "Building DFU image $TARGET",
            ),
        ),
    )
)

if not env.get("PIOFRAMEWORK"):
    raise SystemExit("platform-gr551x : seul le framework « arduino » est pris en charge")

if env.get("PROGNAME", "program") == "program":
    env.Replace(PROGNAME="firmware")

target_elf = env.BuildProgram()
target_bin = env.ElfToBin(join("$BUILD_DIR", "${PROGNAME}"), target_elf)
target_dfu = env.DfuImage(join("$BUILD_DIR", "${PROGNAME}_dfu.bin"), [target_bin, target_elf])
env.Depends(target_dfu, join(TOOLS, "mkimage.py"))

AlwaysBuild(env.Alias("nobuild", target_dfu))
target_buildprog = env.Alias("buildprog", target_dfu, target_dfu)

target_size = env.AddPlatformTarget("size", target_elf, env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"),
                      "Program Size", "Calculate program size")

# ---------------------------------------------------------------- téléversement BLE
upload_cmd = '"$PYTHONEXE" "%s" flash "$SOURCE"' % join(TOOLS, "t1000_ble.py")
enter = env.GetProjectOption("custom_upload_enter_dfu", "")
if enter:
    upload_cmd += ' --enter-dfu "%s"' % enter
env.Replace(UPLOADCMD=upload_cmd)
upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE over BLE DFU")]
env.AddPlatformTarget("upload", target_dfu, upload_actions, "Upload")

Default([target_buildprog, target_size])
