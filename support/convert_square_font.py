#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert TTF font to LVGL C font"
    )

    parser.add_argument(
        "--font",
        default="Square.ttf",
        help="Path to TTF font file",
    )
    parser.add_argument(
        "--size",
        type=int,
        default=18,
        help="Font size in pixels",
    )
    parser.add_argument(
        "--bpp",
        type=int,
        default=4,
        choices=[1, 2, 3, 4, 8],
        help="Bits per pixel",
    )
    parser.add_argument(
        "--range",
        dest="glyph_range",
        default="0x20-0x7E",
        help="Unicode range",
    )
    parser.add_argument(
        "--name",
        default="square_18",
        help="Generated LVGL font symbol name",
    )
    parser.add_argument(
        "--out",
        default="square_18.c",
        help="Output C file path (default: current directory)",
    )

    args = parser.parse_args()

    # Xuất ra thư mục hiện tại nếu chỉ truyền tên file
    out_path = Path(args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        "npx",
        "lv_font_conv",
        "--font",
        args.font,
        "--size",
        str(args.size),
        "--bpp",
        str(args.bpp),
        "--format",
        "lvgl",
        "--range",
        args.glyph_range,
        "--lv-include",
        "lvgl.h",
        "--lv-font-name",
        args.name,
        "--no-kerning",
        "--no-compress",
        "-o",
        str(out_path),
    ]

    print("Running:")
    print(" ".join(cmd))

    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        print(
            f"\n❌ Font conversion failed with exit code {exc.returncode}",
            file=sys.stderr,
        )
        return exc.returncode

    print(f"\n✅ Generated: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())