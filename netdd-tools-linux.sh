#!/bin/bash

# netdd-tools-linux.sh
# 多功能  Linux 快照 + netdd 辅助工具脚本
# 作者：  Liang Zhi Gang
# （支持 /dev/sdX、loop 镜像、LVM、任意块设备）

set -e

# ---------- 参数 ----------
ORIG_DEV="$1"         # 原始块设备，如 /dev/sdb、/dev/loop0、/dev/mapper/vg-lv
TARGET_IP="$2"        # netdd 接收端 IP
PORT="$3"             # netdd 接收端端口

if [ $# -ne 3 ]; then
  echo "用法: $0 <源块设备> <目标IP> <端口>"
  echo "示例: $0 /dev/sdb 10.10.10.24 12345"
  exit 1
fi

# ---------- 准备 ----------
SNAP_NAME="netdd_snap_$$"
SNAP_DEV="/dev/mapper/$SNAP_NAME"
COW_FILE="/tmp/netdd_cow_$$.img"
COW_LOOP=""

echo "[*] 获取扇区数量..."
SECTORS=$(blockdev --getsz "$ORIG_DEV")
echo "    原始设备大小: $SECTORS 扇区"

echo "[*] 创建临时 COW 文件 (512MB)..."
dd if=/dev/zero of="$COW_FILE" bs=1M count=512 status=none
COW_LOOP=$(losetup -f)
losetup "$COW_LOOP" "$COW_FILE"
echo "    COW loop 设备: $COW_LOOP"

# ---------- 创建快照 ----------
echo "[*] 创建 snapshot 设备 $SNAP_NAME"
dmsetup create "$SNAP_NAME" --table "0 $SECTORS snapshot $ORIG_DEV $COW_LOOP P 8"
echo "    快照设备路径: $SNAP_DEV"

# ---------- 快照挂载测试 ----------
MNT_TEST="/mnt/netdd_snap_$$"
mkdir -p "$MNT_TEST"
mount -o ro "$SNAP_DEV" "$MNT_TEST" 2>/dev/null && {
  echo "[✓] 成功挂载快照，文件系统有效"
  umount "$MNT_TEST"
} || {
  echo "[!] 警告：快照设备无法挂载（可能不是标准分区），继续传输块设备..."
}
rm -rf "$MNT_TEST"

# ---------- 启动 netdd_send ----------
echo "[*] 启动 netdd_send 传输数据..."
./netdd_send "$TARGET_IP" "$PORT" "$SNAP_DEV"
echo "[✓] 数据传输完成"

# ---------- 清理 ----------
echo "[*] 清理 snapshot 和临时文件..."
dmsetup remove "$SNAP_NAME"
losetup -d "$COW_LOOP"
rm -f "$COW_FILE"
echo "[√] 全部清理完成。"

exit 0
