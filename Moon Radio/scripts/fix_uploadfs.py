Import("env")

from SCons.Script import COMMAND_LINE_TARGETS


# Ennél az ESP32-S3 lapnál az esptool RAM-ba töltött stubja a nagy
# LittleFS-partíció első tömörített blokkjánál elveszíti a kapcsolatot.
# ROM bootloaderrel, tömörítés és animált folyamatjelző nélkül stabil.
if "uploadfs" in COMMAND_LINE_TARGETS:
    mcu = env.BoardConfig().get("build.mcu", "esp32s3")
    before_reset = env.BoardConfig().get("upload.before_reset", "default-reset")
    after_reset = env.BoardConfig().get("upload.after_reset", "hard-reset")

    env.Replace(
        UPLOADERFLAGS=[
            "--chip",
            mcu,
            "--port",
            '"$UPLOAD_PORT"',
            "--baud",
            "$UPLOAD_SPEED",
            "--before",
            before_reset,
            "--after",
            after_reset,
            "--no-stub",
            "write-flash",
            "--no-progress",
            "$FS_START",
        ],
        UPLOADCMD='$UPLOADER $UPLOADERFLAGS "$SOURCE"',
    )
