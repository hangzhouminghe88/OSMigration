
#!/bin/bash

if [[ $# -ne 2 ]]; then
  echo "用法: $0 <块大小，如 1G> <qcow2 文件名>"
  echo "示例: $0 1G dbrecover.qcow2"
  exit 1
fi

SIZE="$1"
FILE="$2"
PREFIX="${FILE}.part."

echo "🔧 正在将 $FILE 拆分为多个 $SIZE 块..."
split -b "$SIZE" "$FILE" "$PREFIX"

echo "✅ 拆分完成：生成前缀为 $PREFIX 的文件"

~
~
~
