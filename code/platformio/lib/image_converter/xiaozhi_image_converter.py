import os
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageSequence, UnidentifiedImageError


class ImageConverterApp:
    SUPPORTED_UI_FORMATS = ("PNG", "JPG", "BMP", "GIF", "WEBP")

    def __init__(self, master):
        self.master = master
        self.master.title("小智AI 图片批量转换工具")
        self.master.geometry("760x640")

        self.mode = tk.StringVar(value="convert")
        self.output_format = tk.StringVar(value="PNG")
        self.output_dir = tk.StringVar(value=os.path.abspath("output"))
        self.resize_width = tk.IntVar(value=320)
        self.resize_height = tk.IntVar(value=240)
        self.keep_ratio = tk.BooleanVar(value=True)
        self.quality = tk.IntVar(value=90)
        self.enable_optimize = tk.BooleanVar(value=True)

        self.convert_all_btn = None
        self.convert_selected_btn = None

        self.create_widgets()
        self.redirect_output()

    def create_widgets(self):
        mode_frame = ttk.LabelFrame(self.master, text="转换模式")
        mode_frame.grid(row=0, column=0, padx=10, pady=5, sticky="ew")

        ttk.Radiobutton(
            mode_frame,
            text="仅格式转换",
            variable=self.mode,
            value="convert",
            command=self.toggle_settings,
            width=14,
        ).grid(row=0, column=0, padx=5)
        ttk.Radiobutton(
            mode_frame,
            text="缩放 + 格式转换",
            variable=self.mode,
            value="resize_convert",
            command=self.toggle_settings,
            width=16,
        ).grid(row=0, column=1, padx=5)

        self.resize_frame = ttk.LabelFrame(self.master, text="缩放设置")
        self.resize_frame.grid(row=1, column=0, padx=10, pady=5, sticky="ew")

        ttk.Label(self.resize_frame, text="宽").grid(row=0, column=0, padx=2)
        ttk.Entry(self.resize_frame, textvariable=self.resize_width, width=7).grid(
            row=0, column=1, padx=2
        )
        ttk.Label(self.resize_frame, text="高").grid(row=0, column=2, padx=2)
        ttk.Entry(self.resize_frame, textvariable=self.resize_height, width=7).grid(
            row=0, column=3, padx=2
        )
        ttk.Checkbutton(
            self.resize_frame,
            text="保持比例",
            variable=self.keep_ratio,
            width=10,
        ).grid(row=0, column=4, padx=6)

        format_frame = ttk.LabelFrame(self.master, text="输出设置")
        format_frame.grid(row=2, column=0, padx=10, pady=5, sticky="ew")

        ttk.Label(format_frame, text="输出格式").grid(row=0, column=0, padx=4)
        self.format_combo = ttk.Combobox(
            format_frame,
            textvariable=self.output_format,
            values=list(self.SUPPORTED_UI_FORMATS),
            width=8,
            state="readonly",
        )
        self.format_combo.grid(row=0, column=1, padx=4)
        self.format_combo.bind("<<ComboboxSelected>>", self.on_format_change)

        self.quality_label = ttk.Label(format_frame, text="质量")
        self.quality_label.grid(row=0, column=2, padx=4)
        self.quality_spin = ttk.Spinbox(
            format_frame,
            from_=1,
            to=100,
            textvariable=self.quality,
            width=6,
        )
        self.quality_spin.grid(row=0, column=3, padx=4)

        ttk.Checkbutton(
            format_frame,
            text="启用优化",
            variable=self.enable_optimize,
            width=10,
        ).grid(row=0, column=4, padx=8)

        file_frame = ttk.LabelFrame(self.master, text="输入文件")
        file_frame.grid(row=3, column=0, padx=10, pady=5, sticky="nsew")

        ttk.Button(file_frame, text="选择文件", command=self.select_files, width=12).grid(
            row=0, column=0, padx=5, pady=2
        )
        ttk.Button(file_frame, text="移除选中", command=self.remove_selected, width=12).grid(
            row=0, column=1, padx=5, pady=2
        )
        ttk.Button(file_frame, text="清空列表", command=self.clear_files, width=12).grid(
            row=0, column=2, padx=5, pady=2
        )

        self.tree = ttk.Treeview(
            file_frame,
            columns=("selected", "filename"),
            show="headings",
            height=8,
        )
        self.tree.heading("selected", text="选中", anchor=tk.W)
        self.tree.heading("filename", text="文件名", anchor=tk.W)
        self.tree.column("selected", width=60, anchor=tk.W)
        self.tree.column("filename", width=650, anchor=tk.W)
        self.tree.grid(row=1, column=0, columnspan=3, sticky="nsew", padx=5, pady=2)
        self.tree.bind("<ButtonRelease-1>", self.on_tree_click)

        output_frame = ttk.LabelFrame(self.master, text="输出目录")
        output_frame.grid(row=4, column=0, padx=10, pady=5, sticky="ew")

        ttk.Entry(output_frame, textvariable=self.output_dir, width=65).grid(
            row=0, column=0, padx=5, sticky="ew"
        )
        ttk.Button(output_frame, text="浏览", command=self.select_output_dir, width=8).grid(
            row=0, column=1, padx=5
        )

        button_frame = ttk.Frame(self.master)
        button_frame.grid(row=5, column=0, padx=10, pady=10, sticky="ew")

        self.convert_all_btn = ttk.Button(
            button_frame,
            text="转换全部文件",
            command=lambda: self.start_conversion(True),
            width=15,
        )
        self.convert_all_btn.pack(side=tk.LEFT, padx=5)

        self.convert_selected_btn = ttk.Button(
            button_frame,
            text="转换选中文件",
            command=lambda: self.start_conversion(False),
            width=15,
        )
        self.convert_selected_btn.pack(side=tk.LEFT, padx=5)

        log_frame = ttk.LabelFrame(self.master, text="日志")
        log_frame.grid(row=6, column=0, padx=10, pady=5, sticky="nsew")

        self.log_text = tk.Text(log_frame, height=14, width=80)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        self.master.columnconfigure(0, weight=1)
        self.master.rowconfigure(3, weight=1)
        self.master.rowconfigure(6, weight=3)
        file_frame.columnconfigure(0, weight=1)
        file_frame.rowconfigure(1, weight=1)

        self.toggle_settings()
        self.on_format_change()

    def toggle_settings(self):
        if self.mode.get() == "resize_convert":
            self.resize_frame.grid()
        else:
            self.resize_frame.grid_remove()

    def on_format_change(self, _event=None):
        fmt = self.output_format.get().upper()
        use_quality = fmt in {"JPG", "JPEG", "WEBP"}
        if use_quality:
            self.quality_label.state(["!disabled"])
            self.quality_spin.state(["!disabled"])
        else:
            self.quality_label.state(["disabled"])
            self.quality_spin.state(["disabled"])

    def select_files(self):
        file_types = [
            ("图片文件", "*.png *.jpg *.jpeg *.bmp *.gif *.webp"),
            ("全部文件", "*.*"),
        ]

        files = filedialog.askopenfilenames(filetypes=file_types)
        if not files:
            return

        existing_paths = {
            self.tree.item(item, "tags")[0]
            for item in self.tree.get_children()
            if self.tree.item(item, "tags")
        }

        for file_path in files:
            if file_path in existing_paths:
                continue
            self.tree.insert(
                "",
                tk.END,
                values=("[ ]", os.path.basename(file_path)),
                tags=(file_path,),
            )

    def on_tree_click(self, event):
        region = self.tree.identify("region", event.x, event.y)
        if region != "cell":
            return

        col = self.tree.identify_column(event.x)
        item = self.tree.identify_row(event.y)
        if col != "#1" or not item:
            return

        current_val = self.tree.item(item, "values")[0]
        new_val = "[√]" if current_val == "[ ]" else "[ ]"
        self.tree.item(item, values=(new_val, self.tree.item(item, "values")[1]))

    def remove_selected(self):
        to_remove = [
            item
            for item in self.tree.get_children()
            if self.tree.item(item, "values")[0] == "[√]"
        ]
        for item in reversed(to_remove):
            self.tree.delete(item)

    def clear_files(self):
        for item in self.tree.get_children():
            self.tree.delete(item)

    def select_output_dir(self):
        path = filedialog.askdirectory()
        if path:
            self.output_dir.set(path)

    def redirect_output(self):
        class StdoutRedirector:
            def __init__(self, text_widget, callback):
                self.text_widget = text_widget
                self.callback = callback
                self.original_stdout = sys.stdout

            def write(self, message):
                self.callback(message)
                self.original_stdout.write(message)

            def flush(self):
                self.original_stdout.flush()

        sys.stdout = StdoutRedirector(self.log_text, self._append_log)

    def _append_log(self, message):
        self.master.after(0, self._append_log_impl, message)

    def _append_log_impl(self, message):
        self.log_text.insert(tk.END, message)
        self.log_text.see(tk.END)

    def start_conversion(self, convert_all):
        selected_format = self.output_format.get().upper().strip()
        if not self._is_supported_output_format(selected_format):
            invalid_format = selected_format if selected_format else "(空)"
            messagebox.showerror("错误", f"不支持的输出格式: {invalid_format}")
            return

        input_files = []
        for item in self.tree.get_children():
            if convert_all or self.tree.item(item, "values")[0] == "[√]":
                file_tags = self.tree.item(item, "tags")
                if file_tags:
                    input_files.append(file_tags[0])

        if not input_files:
            message = "没有找到可转换的文件" if convert_all else "没有选中任何文件"
            messagebox.showwarning("警告", message)
            return

        try:
            width = int(self.resize_width.get())
            height = int(self.resize_height.get())
            if width <= 0 or height <= 0:
                raise ValueError
        except ValueError:
            messagebox.showerror("错误", "宽和高必须是大于0的整数")
            return

        os.makedirs(self.output_dir.get(), exist_ok=True)

        self.convert_all_btn.state(["disabled"])
        self.convert_selected_btn.state(["disabled"])

        worker = threading.Thread(
            target=self.convert_images,
            args=(input_files, selected_format),
            daemon=True,
        )
        worker.start()

    def convert_images(self, input_files, selected_format):
        selected_format = selected_format.upper().strip()
        if not self._is_supported_output_format(selected_format):
            print(f"不支持的输出格式: {selected_format}")
            self.master.after(0, self._on_convert_finished)
            return

        target_format = self._to_pillow_format(selected_format)
        extension = self._format_to_extension(selected_format)
        print(f"开始转换，共 {len(input_files)} 个文件，目标格式: {selected_format}")

        success = 0
        fail = 0

        try:
            for input_path in input_files:
                try:
                    filename = os.path.basename(input_path)
                    base_name = os.path.splitext(filename)[0]
                    output_path = self._build_non_conflict_path(base_name, extension)

                    print(f"正在转换: {filename}")
                    self.convert_one_image(input_path, output_path, target_format)
                    print(f"转换成功: {filename} -> {os.path.basename(output_path)}\n")
                    success += 1
                except (UnidentifiedImageError, OSError, ValueError, KeyError) as exc:
                    print(f"转换失败: {os.path.basename(input_path)}")
                    print(f"错误信息: {exc}\n")
                    fail += 1
        finally:
            print(f"转换结束，成功 {success} 个，失败 {fail} 个")
            self.master.after(0, self._on_convert_finished)

    def _on_convert_finished(self):
        self.convert_all_btn.state(["!disabled"])
        self.convert_selected_btn.state(["!disabled"])
        messagebox.showinfo("完成", "图片转换已完成，请查看日志")

    def convert_one_image(self, input_path, output_path, target_format):
        with Image.open(input_path) as image:
            if target_format == "GIF":
                self._save_as_gif(image, output_path)
                return

            frame = self._get_first_frame(image)
            frame = self._resize_if_needed(frame)
            frame = self._convert_for_format(frame, target_format)
            save_kwargs = self._build_save_kwargs(target_format)
            frame.save(output_path, format=target_format, **save_kwargs)

    def _save_as_gif(self, image, output_path):
        if getattr(image, "is_animated", False):
            frames = []
            durations = []

            for frame in ImageSequence.Iterator(image):
                converted = self._convert_for_format(frame.copy(), "GIF")
                converted = self._resize_if_needed(converted)
                frames.append(converted)
                durations.append(frame.info.get("duration", 80))

            loop = image.info.get("loop", 0)
            frames[0].save(
                output_path,
                save_all=True,
                append_images=frames[1:],
                loop=loop,
                duration=durations,
                optimize=self.enable_optimize.get(),
            )
            return

        frame = self._convert_for_format(image.copy(), "GIF")
        frame = self._resize_if_needed(frame)
        frame.save(output_path, format="GIF", optimize=self.enable_optimize.get())

    def _get_first_frame(self, image):
        if getattr(image, "is_animated", False):
            image.seek(0)
        return image.copy()

    def _resize_if_needed(self, image):
        if self.mode.get() != "resize_convert":
            return image

        width = max(1, int(self.resize_width.get()))
        height = max(1, int(self.resize_height.get()))

        if self.keep_ratio.get():
            resized = image.copy()
            resized.thumbnail((width, height), Image.Resampling.LANCZOS)
            return resized

        return image.resize((width, height), Image.Resampling.LANCZOS)

    def _convert_for_format(self, image, target_format):
        if target_format in {"JPG", "JPEG"}:
            return self._to_jpeg_safe_rgb(image)

        if target_format == "BMP":
            if image.mode not in {"RGB", "L"}:
                return image.convert("RGB")
            return image

        if target_format == "GIF":
            if image.mode != "P":
                return image.convert("P", palette=Image.Palette.ADAPTIVE)
            return image

        if target_format == "WEBP":
            if image.mode in {"RGB", "RGBA"}:
                return image
            if self._has_alpha(image):
                return image.convert("RGBA")
            return image.convert("RGB")

        if target_format == "PNG":
            if image.mode == "P" and self._has_alpha(image):
                return image.convert("RGBA")
            return image

        return image

    def _to_jpeg_safe_rgb(self, image):
        if self._has_alpha(image):
            rgba = image.convert("RGBA")
            background = Image.new("RGB", rgba.size, (255, 255, 255))
            background.paste(rgba, mask=rgba.split()[-1])
            return background

        if image.mode != "RGB":
            return image.convert("RGB")
        return image

    def _has_alpha(self, image):
        if image.mode in {"RGBA", "LA"}:
            return True
        if image.mode == "P" and "transparency" in image.info:
            return True
        return False

    def _build_save_kwargs(self, target_format):
        kwargs = {}
        if target_format in {"JPG", "JPEG", "WEBP"}:
            quality = min(100, max(1, int(self.quality.get())))
            kwargs["quality"] = quality
            kwargs["optimize"] = self.enable_optimize.get()

        if target_format == "PNG":
            kwargs["optimize"] = self.enable_optimize.get()

        return kwargs

    def _format_to_extension(self, target_format):
        ext_map = {
            "PNG": "png",
            "JPG": "jpg",
            "JPEG": "jpg",
            "BMP": "bmp",
            "GIF": "gif",
            "WEBP": "webp",
        }
        return ext_map.get(target_format, target_format.lower())

    def _to_pillow_format(self, target_format):
        normalized = target_format.upper()
        if normalized == "JPG":
            return "JPEG"
        return normalized

    def _is_supported_output_format(self, output_format):
        return output_format.upper() in {"PNG", "JPG", "JPEG", "BMP", "GIF", "WEBP"}

    def _build_non_conflict_path(self, base_name, extension):
        output_dir = self.output_dir.get()
        candidate = os.path.join(output_dir, f"{base_name}.{extension}")
        if not os.path.exists(candidate):
            return candidate

        index = 1
        while True:
            candidate = os.path.join(output_dir, f"{base_name}_{index}.{extension}")
            if not os.path.exists(candidate):
                return candidate
            index += 1


if __name__ == "__main__":
    root = tk.Tk()
    try:
        app = ImageConverterApp(root)
        root.mainloop()
    except UnidentifiedImageError as error:
        messagebox.showerror("错误", f"无法识别图片格式: {error}")
