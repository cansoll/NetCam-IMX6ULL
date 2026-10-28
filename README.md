# NetCam：嵌入式 Linux 网络图像采集与远程预览系统

基于 i.MX6ULL 开发板与 USB 摄像头，实现 MJPEG 图像的连续采集、TCP 网络传输，并在 Ubuntu (PC端) 进行实时解码与预览。

## 📸 项目演示


## 🛠 技术栈
*   **设备端：** C, Linux, V4L2, MMAP, pthread, TCP Socket
*   **PC 端：** C, TCP Socket, libjpeg, SDL2, Makefile

## 📐 系统架构
1. **板端（Board）：** V4L2 采集 -> 环形队列缓存 -> 自定义协议打包 (Header+JPEG) -> TCP 发送。
2. **PC端（PC）：** TCP 接收 -> 拼包解析 -> libjpeg 解码为 RGB -> SDL2 渲染。

## 🚀 编译与运行

### 1. PC 端 (Ubuntu)
```bash
# 安装依赖
sudo apt install libjpeg-dev libsdl2-dev

cd pc/
make
# 运行 (-p 指定端口, -s 开启按 S 键截图)
./pc_preview -p 8888 -s