# 内置界面字体

客户端界面使用以下两款开源字体，编译时作为 RCDATA 资源写入 EXE，运行时仅在本进程内注册（DirectWrite 内存字体集 + GDI 私有字体），不安装到系统。

| 文件 | 字体 | 字重 |
| --- | --- | --- |
| NotoSansSC-Regular/Medium/SemiBold.ttf | Noto Sans SC 2.004 | 400 / 500 / 600 |
| JetBrainsMono-Regular/Medium/SemiBold/Bold.ttf | JetBrains Mono 2.211 | 400 / 500 / 600 / 700 |

两款字体均采用 SIL Open Font License 1.1，许可证全文见 [OFL.txt](OFL.txt)。

为控制 EXE 体积，Noto Sans SC 只保留 ASCII、GB2312 符号与一级常用汉字（3755 字）及客户端源码中出现的全部字符；方案名中的其他字符由 DirectWrite 自动回退到 Windows 自带字体。重新生成请运行 `scripts/subset-fonts.py`（需 fonttools）。
