# Finality Manager

GTK4 桌面工具，用于管理多个 Age of History 3 实例。选择游戏根目录即可添加单个实例；选择 SteamLibrary（或其包含 `steamapps/common` 的目录）会自动发现其中所有包含 `game.jar` 的安装。

在 Linux 上，程序还会自动检测 Steam 安装目录（包括 `~/.steam`、`~/.local/share/Steam`、
Flatpak 与 Snap 路径），解析各库的 `steamapps/libraryfolders.vdf` 以枚举所有 SteamLibrary，
然后从中解析出游戏实例。首次运行且尚无配置文件时会自动执行；也可点击标题栏的搜索按钮
随时手动重新检测。

点击“启动游戏”时，程序会把应用资源中的 `res/finality-loader-1.6.0.jar` 覆盖复制到实例根目录，然后在该目录使用 Loader2 启动。Loader2 接收 `--core` 和 `--mods` 参数；如果实例自带 `jre/bin/java`，会优先使用它。

如果 Age of History 3 实例根目录下存在 `multi.jar`，程序会额外添加一个名为
“Age of History 3 MP”的实例。启动该实例时优先使用目录下的 `jdk/bin/java`，并把
Loader2 会将 `./multi.jar` 作为 MP 实例的 `--core`，工作目录仍为游戏根目录。

“模组”页会读取所选实例 `mods/` 目录中的模组，并从模组的 `mod.txt` 显示名称和描述。开关状态与游戏使用的 `settings/ModsOff.txt` 同步，会在下次启动游戏时生效。

## 播放集

每个实例可以保存多套模组组合（播放集）。在“播放集”窗口用单选按钮选中一个播放集，主界面的
“启动游戏”就会按它启动：先启用播放集中的模组、禁用该实例的其他模组，再启动游戏。播放集可以
新建、改名、编辑、移除（至少保留一个，可用“编辑”或主界面的“管理模组”修改其模组列表）。

主界面会显示当前播放集。实例还没有任何播放集时会自动创建一个名为 `Default` 的播放集，内容为
该实例当前的启用情况，因此不改播放集直接启动与以前的行为一致。

“管理模组”窗口就是当前播放集的模组列表：勾选表示在该播放集中启用。支持全选/全不选（只作用于
当前筛选出的模组）、按来源（本地/Steam）与启用状态过滤，以及按标题搜索模组。

播放集按实例存储，位于实例根目录的 `.finality/manager/playlists.properties`；启动时生成的 Loader 模组列表
写入同一目录的 `modlist.txt`，内容为 Loader2 接受的 JSON 模组目录数组，并通过 `--mods` 参数传给 Loader2。
Loader2 只加载列表中的模组。启用状态同时写入 `settings/ModsOff.txt`，保证游戏本体的模组加载与播放集一致。
播放集未包含 Finality API 时 Loader 会提示找不到 API。

## 工具与进程状态

实例详情页“浏览模组文件”左侧的扳手按钮提供两个高级操作：

- “生成模组列表定义”：只根据当前播放集写出 `<实例>/.finality/manager/modlist.txt`，不修改启用状态；
- “使用 Loader2 启动”：先应用当前播放集，再以 Loader2 启动当前游戏实例。

日志窗口底部会显示对应游戏进程的状态（运行中 / 已关闭）。进程退出后自动更新，
“终止游戏进程”按钮在进程结束后不可用。

## 依赖

- GTK 4 开发包
- GCC（或其他兼容 C 编译器）
- `pkg-config`

## 构建与运行

```sh
make
./build/linux/finality-manager
```

不放心的可以跑一遍无界面测试（模组发现、ModsOff.txt 与播放集读写）：

```sh
make test
```

Windows 下还有一个针对“日志窗口捕获不到 Java 输出”的回归测试（需要系统里有 Java，
找不到时会自动 SKIP 并通过）：

```sh
make test-windows
```

在 Linux 上通过 Wine 运行时会使用 `FINALITY_TEST_JAVA=/path/to/java.exe` 指定 Java。
该测试必须以 GUI 子系统构建，才能复现“无控制台导致 GSubprocess 丢失子进程 stdout”的问题；
应用在 `app_activate` 里会调用 `win32_prepare_child_stdio()` 分配一个隐藏控制台来修复它。

## 构建 AppImage

先安装 [appimagetool](https://github.com/AppImage/appimagetool)，并确保它在 `PATH` 中，然后执行：

```sh
make app-image
```

产物为 `build/appImage/Finality-Manager-x86_64.AppImage`。如果工具不在 `PATH` 中，可通过
`APPIMAGE_TOOL=/path/to/appimagetool make app-image` 指定路径。

## 交叉编译 Windows x86_64

需要已配置好 GTK4 的 MinGW 开发环境（包括 `x86_64-w64-mingw32-gcc` 和对应的
GTK4 `.pc` 文件）。Makefile 也支持 quasi-msys2 的 UCRT64 sysroot（默认路径为
`~/quasi-msys2/root/ucrt64`），在 Linux 主机上：

```sh
make windows
```

产物为 `build/windows/finality-manager.exe`。如果使用 Wine 运行，需要把 GTK4
运行时 DLL 加入搜索路径：

```sh
WINEPATH="$HOME/quasi-msys2/root/ucrt64/bin" wine build/windows/finality-manager.exe
```

也可以直接运行 `make run-windows`。如果工具链前缀不同，可覆盖
`WINDOWS_TRIPLE`，例如 `make windows WINDOWS_TRIPLE=x86_64-pc-windows-gnu`；
也可以直接覆盖 `CC`、`PKG_CONFIG` 和 `WINDOWS_LD`。

## 打包 Windows 运行时

`make windows` 只生成 exe，目标机器上如果没有 MSYS2/GTK4 环境就无法运行。用
`bundle-windows` 会自动把 GTK4/GLib 的 DLL、GSettings schema、GTK 数据、
gdk-pixbuf 加载器、图标主题和 `res/` 资源一并复制到 `build/windows/bundle/`：

```sh
make bundle-windows
```

GTK4 运行时已经随仓库放在 `vendor/ucrt64/`（约 69MB），因此打包不再需要安装
quasi-msys2。打包结果是一个自包含目录，把这个目录整个拷到 Windows 上双击
`finality-manager.exe` 即可运行（不需要额外安装 GTK4）。在 Linux 上可用 Wine
直接验证：

```sh
make run-bundle-windows
```

`vendor/ucrt64/` 里包含了运行时 DLL、GLib 启动子进程用的
`gspawn-win64-helper*.exe`、gdk-pixbuf 加载器和预生成的 `loaders.cache`、
`gschemas.compiled` 以及 GTK 数据，所以打包过程完全不需要 Wine。
图标只保留 GTK4 在 Windows 上会用到的那部分（`Adwaita` 的
`index.theme`/`symbolic`/`scalable`/`16x16` 与 `hicolor`），GTK3 的
`AdwaitaLegacy` 和未被请求的 `Adwaita/cursors` 都不会打包，因此图标体积从约
15MB 降到约 3.5MB。输出目录可用 `BUNDLE_DIR` 覆盖，也可用 `WINDOWS_RUNTIME`
指向别的运行时前缀。

升级 GTK4 版本时，用下面命令从 quasi-msys2 sysroot 重新生成 `vendor/ucrt64/`
（需要已安装 sysroot 和 Wine），然后把改动提交：

```sh
make vendor-windows
```

注意：编译 Windows exe 仍然需要 MinGW-w64 + GTK4 开发环境（头文件、导入库与
pkg-config），`vendor/ucrt64/` 只提供运行时。

## 制作 Windows 安装包

用 NSIS 把打包好的自包含目录做成安装程序（需要 `makensis`，即 `nsis` 包）：

```sh
make nsis
```

产物为 `build/windows/Finality-Manager-Setup.exe`。安装程序会：

- 安装到 `%ProgramFiles%\Finality Manager`，并写入开始菜单与卸载项；
- 可选创建桌面快捷方式；
- 在“添加或删除程序”中登记，可正常卸载；
- 提供简体中文与英文界面。

版本号默认 `1.6.0`，可用 `APP_VERSION` 覆盖，例如
`make nsis APP_VERSION=1.7.0`；输出文件可用 `NSIS_OUTFILE` 覆盖。安装脚本位于
`installer/finality-manager.nsi`。在 Linux 上可用 Wine 运行安装包验证：

```sh
make run-nsis-windows
```

```sh
make clean
```
