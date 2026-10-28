# 占位构建清单（空模块）。拥有者：A4；实现时替换本文件。
# 约定：addon 共享库；实现时 find_package(Fcitx5Core)，缺依赖须 return() 优雅跳过。
# 依赖方向与目标命名见 docs/interfaces/build.md；不得依赖 fcitx5（除 addon）。
# 本文件当前不做任何事，仅保证根骨架按存在性检查纳入模块目录。
