import os

# -------------------------------------------------------
# Chạy được cả trong PlatformIO và Python thường
# -------------------------------------------------------

IS_PLATFORMIO = True

try:
    Import("env")  # Chỉ tồn tại trong PlatformIO
except NameError:
    IS_PLATFORMIO = False


def merge_files(build_dir, output_path):
    print("\n[CUSTOM ACTION] ---> Đang gộp firmware ESP32-S3 <---")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")

    offsets = {
        bootloader: 0x1000,
        partitions: 0x8000,
        firmware: 0x10000,
    }

    # Kiểm tra file
    for f in offsets:
        if not os.path.isfile(f):
            print(f"❌ Không tìm thấy: {f}")
            return

    # Tính kích thước file merge
    max_size = 0
    for f, offset in offsets.items():
        end = offset + os.path.getsize(f)
        if end > max_size:
            max_size = end

    merged = bytearray([0xFF] * max_size)

    # Ghi từng file vào đúng offset
    for f, offset in offsets.items():
        with open(f, "rb") as infile:
            data = infile.read()
            merged[offset:offset + len(data)] = data
            print(f"✔ {os.path.basename(f)} -> 0x{offset:X}")

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with open(output_path, "wb") as out:
        out.write(merged)

    print("\nDone ✅")
    print(f"Output : {output_path}")
    print(f"Size   : {len(merged)} bytes\n")


# -------------------------------------------------------
# PlatformIO
# -------------------------------------------------------

if IS_PLATFORMIO:

    def merge_bin_action(source, target, env):
        build_dir = env.subst("$BUILD_DIR")
        project_dir = env.subst("$PROJECT_DIR")

        output = os.path.join(project_dir, "merged.bin")

        merge_files(build_dir, output)

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin_action)

# -------------------------------------------------------
# Python thường
# -------------------------------------------------------

else:
    PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

    BUILD_DIR = os.path.join(
        PROJECT_DIR,
        ".pio",
        "build",
        "twatchs3",   # đổi nếu tên env khác
    )

    OUTPUT = os.path.join(PROJECT_DIR, "merged.bin")

    merge_files(BUILD_DIR, OUTPUT)