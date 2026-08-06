# image_converter 小智AI 图片批量转换器

本脚本用于批量图片格式转换，界面风格参考 OGG 转换工具，支持以下格式：

- PNG
- JPG
- BMP
- GIF
- WEBP

支持功能：

- 批量添加文件
- 仅转换选中文件
- 输出目录选择
- 可选缩放（保持比例或强制尺寸）
- 日志实时显示

## 创建并激活虚拟环境

```bash
python -m venv venv
venv\Scripts\activate
```

## 安装依赖

```bash
pip install pillow
```

## 运行脚本

```bash
python xiaozhi_image_converter.py
```
