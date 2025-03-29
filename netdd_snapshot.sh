#!/bin/bash

# netdd_snapshot.sh
# 用于对 ext4、xfs 分区在没有 LVM 的情况下创建 snapshot，并通过 netdd 发送
# 作者： 梁芝刚
# 2025 03 28

set -e
# 检查依赖
for cmd in dmsetup blockdev losetup mount umount; do
  if ! command -v $cmd &>/dev/null; then
    echo "[!] 缺少命令: $cmd，请先安装。"
    exit 1
  fi
done

# 参数定义
ORIG_DEV="$1"       # 原始块设备，如 /dev/sdb1
TARGET_IP="$2"      # 接收端 IP
PORT="$3"           # 接收端端口

if [ $# -ne 3 ]; then
  echo "用法: $0 <原始设备> <目标IP> <端口>"
  echo "示例: $0 /dev/sdb1 10.10.10.24 12345"
  exit 1
fi

# 获取扇区数量
SECTORS=$(blockdev --getsz "$ORIG_DEV")

# 创建 COW 文件并挂载 loop 设备
COW_FILE="/tmp/cowfile.$$"
COW_SIZE_MB=1024
COW_LOOP_DEV=""

echo "[*] 创建 $COW_SIZE_MB MB 的 COW 文件..."
dd if=/dev/zero of="$COW_FILE" bs=1M count=$COW_SIZE_MB &>/dev/null
COW_LOOP_DEV=$(losetup -f)
losetup "$COW_LOOP_DEV" "$COW_FILE"

# 创建 snapshot
SNAP_NAME="snap_temp_$$"
SNAP_DEV="/dev/mapper/$SNAP_NAME"
echo "[*] 创建 snapshot 设备: $SNAP_DEV"
dmsetup create "$SNAP_NAME" --table "0 $SECTORS snapshot $ORIG_DEV $COW_LOOP_DEV P 8"

# 检测是否可挂载（ext4/xfs）
echo "[*] 尝试挂载 snapshot 检查文件系统类型..."
mkdir -p /mnt/snap_test_$$
mount -o ro "$SNAP_DEV" /mnt/snap_test_$$ && FS_OK=1 || FS_OK=0

if [ $FS_OK -eq 1 ]; then
  echo "[✓] 快照挂载成功，文件系统正常"
  umount /mnt/snap_test_$$
else
  echo "[!] 警告：快照挂载失败，继续直接传输块设备"
fi
rm -rf /mnt/snap_test_$$

# 使用 netdd 发送
echo "[*] 启动 netdd_send ..."
./netdd_send "$TARGET_IP" "$PORT" "$SNAP_DEV"

# 清理
echo "[*] 清理 snapshot 和 loop ..."
dmsetup remove "$SNAP_NAME"
losetup -d "$COW_LOOP_DEV"
rm -f "$COW_FILE"

echo "[√] 传输完成并清理完毕。"
