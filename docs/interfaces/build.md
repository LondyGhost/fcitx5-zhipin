# 构建骨架契约（build.md）

拥有方：A0（根 `CMakeLists.txt`）。模块 `CMakeLists.txt` 由各自任务拥有。
关联接口版本 1.0.0（状态见 [README.md](README.md)）。

## 1. 工具链与语言

| 项 | 冻结值 | 依据 |
|---|---|---|
| CMake 最低版本 | `3.16` | 目标机实测 4.4.4 兼容；CI/本机旧版可配置 |
| C++ 标准 | **C++17**（`CMAKE_CXX_STANDARD 17`，无扩展） | 目标机 GCC 16.2.1；fcitx5 5.x 自身要求 C++17，故存在 fcitx5 即存在 C++17 |
| 生成器 | Ninja / Make 均可 | 目标机 Ninja 1.13.2、GNU Make 4.4.1 |
| 编译告警 | `-Wall -Wextra -Wpedantic`（GCC/Clang）；`/W4 /permissive-`（MSVC） | 经 `abc_build_options` 接口目标继承 |
| 第三方依赖 | **无**（核心与导入器零依赖；addon 仅依赖目标机 fcitx5 开发包） | 保持骨架在最小环境可构建 |

## 2. 根构建骨架的模块注册规则

根 `CMakeLists.txt` 按下表**存在性检查**后 `add_subdirectory`：
模块目录里出现 `CMakeLists.txt` 即自动纳入构建，**无需编辑根文件**。

| 模块目录 | 构建清单拥有者 | 说明 |
|---|---|---|
| `source/core` | A1 | 核心库目标 `abc_core`（实现 `createEngineSession` 等） |
| `source/dict-importer` | A2 | 可执行 `abc_dict_import` |
| `source/dictionary` | A3 | 库目标 `abc_dictionary`（`loadDictionaryFromFile` 等） |
| `source/fcitx5-addon` | A4 | addon 共享库；内部自行 `find_package(Fcitx5Core ...)`，找不到时**必须优雅跳过**（`return()`），不得让骨架配置失败 |
| `source/config` | A5 | 库目标 `abc_config`（配置解析） |
| `tests/smoke` | A0 | 接口冒烟测试（已交付） |
| `tests/core` / `tests/dict-importer` / `tests/dictionary` / `tests/fcitx5-addon` / `tests/deploy` | A1/A2/A3/A4/A5 | 各任务自行创建 `CMakeLists.txt` 后自动纳入 |

规则：

1. 上表之外的模块目录必须先由 A0/集成负责人登记进根清单，禁止并行任务直接改根文件。
2. 各模块目标名必须使用上表前缀（`abc_*`），避免冲突。
3. 测试目标一律 `add_test` 纳入 CTest；测试可执行名以 `abc_` 开头。
4. 模块不得修改其他模块目录内的任何文件；跨模块复用通过 `abc_interfaces` 公共头或 CMake 目标链接完成。
5. 根文件定义的全局开关：`ABC_BUILD_TESTS`（默认 `ON`）控制 `tests/` 是否纳入。

## 3. 公共 CMake 目标（A0 定义于根文件）

| 目标 | 类型 | 内容 |
|---|---|---|
| `abc_interfaces` | INTERFACE | `source/include` 头文件包含路径 + `cxx_std_17`。**所有模块对公共头的依赖只允许经它**。 |
| `abc_build_options` | INTERFACE | 统一告警选项；模块目标私有链接它。 |

## 4. 依赖方向

```text
                    ┌──────────────────────────────┐
                    │ abc_interfaces (source/include/abc) │
                    └──────────────────────────────┘
                      ▲        ▲        ▲        ▲        ▲
           ┌──────────┘        │        │        │        └──────────┐
      abc_core          abc_dictionary  abc_config   abc_dict_import   abc_fcitx5_addon
      (A1)              (A3)            (A5)         (A2)              (A4)
  │                 │               │            │                 │
       └── 仅标准库      └── 仅标准库    └─ 仅标准库  └─ 仅标准库      └─ + fcitx5 开发包
```

- `abc_core`、`abc_dictionary`、`abc_config`、`abc_dict_import`：**禁止**链接/包含 fcitx5、X11、Wayland 任何头或库；只依赖 `abc_interfaces` 与标准库。
- `abc_fcitx5_addon`：允许 fcitx5 开发包；依赖 A1 `abc_core`、A3 `abc_dictionary`、A5 `abc_config`，将其静态归档链接进 addon；三库须 PIC。核心和数据模块仍不依赖 fcitx5。
- **核心不得反向依赖 addon**；头文件层由冒烟测试强制（扫描 `#include`）。
- 会话工厂（A1）与词典加载器（A3）的符号由其模块提供；骨架期无实现时冒烟测试不得引用（见 `tests/smoke`）。

## 5. 标准构建/测试命令（干净环境）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

- 干净环境定义：全新 clone/目录、无既有 build 目录与缓存。
- 安装规则不在根文件：用户级部署由 A5 脚本管理（不写 `/usr`、`/opt`，项目级硬性边界）。
- 冒烟测试通过标准：`ctest` 0 failed（当前仅 `abc_interface_smoke`）。

## 6. 目标机实测记录（2026-10-28 采集，详见 `docs/目标机环境.md`）

CatOS（Arch 系滚动）x86_64；GCC/G++ 16.2.1；CMake 4.4.4；Ninja 1.13.2；
GNU Make 4.4.1；fcitx5 5.1.23-1（含 `/usr/include/Fcitx5` 头与 `Fcitx5Core` 等
CMake 包配置，A4 可直接 `find_package`）；4 CPU；`$HOME` 可用空间 141 GB。
