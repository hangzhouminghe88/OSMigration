# netdd
Operating System Migration 操作系统迁移，目前支持 WINDOWS操作系统

用于 工业控制系统 不能与外界联网等情况下冷迁移用
具体看 OS迁移方案.docx

`netdd` 是一个用于通过网络接收裸盘数据并直接写入磁盘设备的工具，支持数据块编号、跳过全零块、显示进度，适用于系统迁移、远程恢复等场景。

## 特性
- 接收并写入裸盘镜像数据
- 跳过全零块以节省空间
- 支持断点编号与跳写逻辑
- 支持 64K 分块传输，尾块自动识别实际长度
- 简洁易用、跨平台支持

## 作者
- ** 梁芝纲 / LIANG ZHI GANG
- GitHub: [https://github.com/zhangsan/netdd](https://github.com/zhangsan/netdd)

## 使用示例
```
客户端
./netdd_send PHYSICALDRIVE0(WINDOWS硬盘0或1或2）12332（端口号）10.10.10.207（远端地址）
服务端：
./netdd 12332 /dev/vda

原理：服务端将监听 12332 端口，将客户端 硬盘数据写入 `/dev/vda` ，结合 RESCUE CD虚拟机 ，就可以直接在 云端启动；

如需加入更多功能（如多盘同时上传、自动识别、增量同步等），欢迎 issue / PR / 交流！



