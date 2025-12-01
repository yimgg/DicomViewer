# myDicomViewer

一个基于 Qt、VTK 和 ITK 的 DICOM 医学图像查看器。

## 功能特性

- 支持读取 DICOM 序列
- 多视图显示：
  - 轴状位（Axial）视图
  - 矢状位（Sagittal）视图
  - 冠状位（Coronal）视图
  - 3D 视图（预留）

## 技术栈

- **Qt 5.15**: GUI 框架
- **VTK 9.2**: 3D 可视化和图像处理
- **ITK 5.3**: 医学图像处理
- **CMake**: 构建系统
- **MSVC 2019**: 编译器

## 系统要求

- Windows 10/11
- Qt 5.15 或更高版本
- VTK 9.2（安装路径：`C:\Program Files\VTK`）
- ITK 5.3（安装路径：`C:\Program Files\ITK`）
- Visual Studio 2019 或更高版本（MSVC 编译器）

## 构建说明

1. 克隆仓库：
```bash
git clone https://github.com/yourusername/myDicomViewer.git
cd myDicomViewer
```

2. 使用 Qt Creator 打开项目：
   - 打开 Qt Creator
   - 选择 "文件" -> "打开文件或项目"
   - 选择 `CMakeLists.txt` 文件

3. 配置 CMake：
   - 如果 VTK 或 ITK 的安装路径不同，请在 CMake 配置中修改 `VTK_DIR` 和 `ITK_DIR` 变量

4. 构建项目：
   - 点击 "构建" -> "构建项目"

## 使用方法

1. 运行程序
2. 点击 "打开" 按钮
3. 选择包含 DICOM 序列的文件夹
4. 图像将自动显示在三个视图中

## 项目结构

```
myDicomViewer/
├── CMakeLists.txt      # CMake 构建配置
├── main.cpp            # 程序入口
├── widget.h            # 主窗口头文件
├── widget.cpp          # 主窗口实现
├── widget.ui           # UI 设计文件
└── README.md           # 项目说明
```

## 许可证

[在此添加您的许可证]

## 贡献

欢迎提交 Issue 和 Pull Request！

