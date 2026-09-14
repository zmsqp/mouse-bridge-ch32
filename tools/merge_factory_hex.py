#!/usr/bin/env python3
"""Merge BOOT and APP Intel HEX files and create a VALID 256-byte APP header."""

from __future__ import annotations

import argparse
import re
import struct
import zlib
from pathlib import Path

BOOT_START = 0x00000000
BOOT_END = 0x00003000
HEADER_START = 0x00003000
HEADER_SIZE = 0x100
APP_START = 0x00003100
APP_END = 0x0000F000
PROFILE_START = 0x0000F000
PROFILE_END = 0x00010000
PHYSICAL_BIAS = 0x08000000

HEADER_MAGIC = 0x31474D49
HEADER_VERSION = 1
STATE_VALID = 0xFFFFFFFC


class MergeError(ValueError):
    pass


def parse_hex(path: Path) -> dict[int, int]:
    memory: dict[int, int] = {}
    base = 0
    eof = False
    for line_number, raw in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if eof or not line.startswith(":") or (len(line) - 1) % 2:
            raise MergeError(f"{path.name} 第 {line_number} 行格式无效")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as exc:
            raise MergeError(f"{path.name} 第 {line_number} 行含非十六进制字符") from exc
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xFF:
            raise MergeError(f"{path.name} 第 {line_number} 行长度或校验和错误")
        count = record[0]
        offset = (record[1] << 8) | record[2]
        kind = record[3]
        data = record[4:-1]
        if kind == 0:
            for index, value in enumerate(data):
                address = base + offset + index
                if 0x08000000 <= address < 0x08010000:
                    address -= PHYSICAL_BIAS
                previous = memory.get(address)
                if previous is not None and previous != value:
                    raise MergeError(f"{path.name} 地址 0x{address:08X} 数据冲突")
                memory[address] = value
        elif kind == 1:
            if count:
                raise MergeError(f"{path.name} EOF 记录长度错误")
            eof = True
        elif kind == 2:
            if count != 2 or offset:
                raise MergeError(f"{path.name} 扩展段地址记录错误")
            base = int.from_bytes(data, "big") << 4
        elif kind == 4:
            if count != 2 or offset:
                raise MergeError(f"{path.name} 扩展线性地址记录错误")
            base = int.from_bytes(data, "big") << 16
        elif kind in (3, 5):
            if count != 4:
                raise MergeError(f"{path.name} 启动地址记录错误")
        else:
            raise MergeError(f"{path.name} 使用不支持的记录类型 0x{kind:02X}")
    if not eof or not memory:
        raise MergeError(f"{path.name} 缺少 EOF 或没有数据")
    return memory


def version_from_filename(path: Path) -> int:
    match = re.search(r"(?i)(?:^|[^a-z0-9])v(\d+)\.(\d+)", path.stem)
    if not match:
        return 0
    major, minor = int(match.group(1)), int(match.group(2))
    if major > 0xFFFF or minor > 0xFFFF:
        raise MergeError("APP 文件名中的版本号超出范围")
    return (major << 16) | minor


def make_header(image: bytes, version: int) -> bytes:
    prefix = struct.pack(
        "<IHHIIIII",
        HEADER_MAGIC,
        HEADER_VERSION,
        HEADER_SIZE,
        len(image),
        zlib.crc32(image) & 0xFFFFFFFF,
        APP_START,
        version,
        0,
    )
    immutable_crc = zlib.crc32(prefix) & 0xFFFFFFFF
    header = prefix + struct.pack(
        "<IIII",
        immutable_crc,
        0xFFFFFFFF,
        0xFFFFFFFF,
        STATE_VALID,
    )
    return header + b"\xFF" * (HEADER_SIZE - len(header))


def _record(address: int, kind: int, data: bytes = b"") -> str:
    body = bytearray([len(data), (address >> 8) & 0xFF, address & 0xFF, kind])
    body.extend(data)
    body.append((-sum(body)) & 0xFF)
    return ":" + body.hex().upper()


def write_hex(path: Path, memory: dict[int, int]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines: list[str] = []
    addresses = sorted(memory)
    index = 0
    current_upper: int | None = None
    while index < len(addresses):
        start = addresses[index]
        upper = start >> 16
        if upper != current_upper:
            if upper:
                lines.append(_record(0, 4, upper.to_bytes(2, "big")))
            current_upper = upper
        chunk = bytearray([memory[start]])
        index += 1
        while (
            index < len(addresses)
            and addresses[index] == start + len(chunk)
            and addresses[index] >> 16 == upper
            and len(chunk) < 16
        ):
            chunk.append(memory[addresses[index]])
            index += 1
        lines.append(_record(start & 0xFFFF, 0, bytes(chunk)))
    lines.append(_record(0, 1))
    path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


def merge_factory(boot_path: Path, app_path: Path, output_path: Path, version: int) -> dict[str, int]:
    boot = parse_hex(boot_path)
    app = parse_hex(app_path)
    if min(boot) != BOOT_START or any(not BOOT_START <= address < BOOT_END for address in boot):
        raise MergeError("BOOT HEX 必须且只能位于 0x00000000–0x00002FFF")
    if APP_START not in app or any(not APP_START <= address < APP_END for address in app):
        raise MergeError("APP HEX 必须且只能位于 0x00003100–0x0000EFFF")

    image_end = max(app) + 1
    image = bytes(app.get(address, 0xFF) for address in range(APP_START, image_end))
    header = make_header(image, version)
    merged = dict(boot)
    for index, value in enumerate(header):
        merged[HEADER_START + index] = value
    for index, value in enumerate(image):
        merged[APP_START + index] = value

    if any(PROFILE_START <= address < PROFILE_END for address in merged):
        raise MergeError("合并结果意外覆盖六枪配置区")
    write_hex(output_path, merged)
    return {
        "boot_bytes": len(boot),
        "app_size": len(image),
        "app_crc32": zlib.crc32(image) & 0xFFFFFFFF,
        "version": version,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="生成 BOOT + VALID APP 工厂合并 HEX")
    parser.add_argument("--boot", type=Path, required=True, help="Bootloader HEX")
    parser.add_argument("--app", type=Path, required=True, help="APP HEX")
    parser.add_argument("--output", type=Path, required=True, help="输出工厂 HEX")
    parser.add_argument("--version", type=lambda value: int(value, 0), help="32 位 APP 版本码")
    return parser.parse_args()


def main():
    args = parse_args()
    version = args.version if args.version is not None else version_from_filename(args.app)
    info = merge_factory(args.boot, args.app, args.output, version)
    print(
        f"工厂 HEX 已生成：{args.output}\n"
        f"BOOT 数据 {info['boot_bytes']}B，APP 镜像 {info['app_size']}B，"
        f"CRC32=0x{info['app_crc32']:08X}，版本=0x{info['version']:08X}\n"
        "六枪配置区 0x0000F000–0x0000FFFF 未写入。"
    )


if __name__ == "__main__":
    main()
