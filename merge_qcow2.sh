
#!/bin/bash

if [[ $# -lt 2 ]]; then
  echo "用法: $0 <输出文件名> <part 文件...>"
  echo "示例: $0 dbrecover_restored.qcow2 dbrecover.qcow2.part.*"
  exit 1
fi

OUT="$1"
shift
cat "$@" > "$OUT"
echo "✅ 合并完成：输出文件 $OUT"

~
~
~
