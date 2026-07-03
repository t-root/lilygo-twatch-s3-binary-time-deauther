import os
import sys

# ===== BASE DIR (thư mục chứa file .bin) =====
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

# ===== FILE INPUT =====
BOOTLOADER = os.path.join(BASE_DIR, "bootloader.bin")
PARTITIONS = os.path.join(BASE_DIR, "partitions.bin")
FIRMWARE = os.path.join(BASE_DIR, "firmware.bin")

# ===== OFFSET CHUẨN ESP32-S3 =====
OFFSETS = {
    BOOTLOADER: 0x1000,
    PARTITIONS: 0x8000,
    FIRMWARE: 0x10000
}

def merge_bin(output_name):
    # kiểm tra file tồn tại
    for f in OFFSETS:
        if not os.path.exists(f):
            print(f"❌ Không tìm thấy: {f}")
            return

    # tính kích thước file output
    max_size = 0
    for f, offset in OFFSETS.items():
        size = os.path.getsize(f)
        end = offset + size
        if end > max_size:
            max_size = end

    # tạo buffer (fill 0xFF)
    merged = bytearray([0xFF] * max_size)

    # ghi từng file vào đúng offset
    for f, offset in OFFSETS.items():
        with open(f, "rb") as infile:
            data = infile.read()
            merged[offset:offset+len(data)] = data
            print(f"✔ Added {os.path.basename(f)} at 0x{offset:X}")

    # ghi ra file output
    output_path = os.path.join(BASE_DIR, output_name)
    with open(output_path, "wb") as out:
        out.write(merged)

    print(f"\n✅ Done → {output_path}")
    print(f"Size: {len(merged)} bytes")

# ===== MAIN =====
if __name__ == "__main__":
    if len(sys.argv) > 1:
        output_file = sys.argv[1]
    else:
        output_file = "merged.bin"

    merge_bin(output_file)