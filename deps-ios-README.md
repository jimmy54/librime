# deps-ios.mk - iOS 依赖库编译配置

这个 Makefile 专门用于为 iOS 平台编译 librime 的依赖库。

## 🎯 用途

- 单独编译 librime 的依赖库
- 支持多个 iOS 平台 (真机、模拟器)
- 不修改原始的 `deps.mk` 文件
- 便于调试和定制编译过程

## 🚀 快速使用

```bash
# 查看帮助
make -f deps-ios.mk help

# 查看配置
make -f deps-ios.mk info

# 编译所有依赖库 (iOS 真机)
make -f deps-ios.mk NOPARALLEL=1

# 编译特定库
make -f deps-ios.mk leveldb NOPARALLEL=1
```

## 📖 详细文档

完整的使用说明请参考: **[../DEPS_BUILD.md](../DEPS_BUILD.md)**

## ⚙️ 配置选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `ios_platform` | OS64 | iOS 平台 (OS64, SIMULATOR64, SIMULATORARM64) |
| `ios_deployment_target` | 13.0 | 最低 iOS 版本 |
| `ios_cmake_toolchain` | ../ios-cmake/ios.toolchain.cmake | 工具链文件路径 |
| `prefix` | $(CURDIR) | 安装路径 |
| `build` | build-ios | 构建目录名 |

## 📦 编译的库

- **glog** - Google 日志库
- **googletest** - Google 测试框架
- **leveldb** - 键值存储数据库
- **marisa-trie** - Trie 树数据结构
- **opencc** - 简繁体转换
- **yaml-cpp** - YAML 解析库

## 🔧 与 deps.mk 的区别

| 特性 | deps.mk | deps-ios.mk |
|------|---------|-------------|
| 目标平台 | macOS/Linux | iOS |
| 工具链 | 系统默认 | ios-cmake |
| 构建目录 | `build` | `build-ios` |
| CMake 生成器 | Unix Makefiles | Xcode |

## 💡 提示

通常情况下,你不需要单独运行这个 Makefile,因为:
1. `build-librime-ios.sh` 脚本会自动处理依赖库编译
2. CMake 会自动为所有子项目应用 iOS 工具链

只有在以下情况下才需要使用此文件:
- 需要单独编译某个依赖库
- 调试依赖库的编译问题
- 为不同平台编译依赖库
- 定制依赖库的编译选项

## 📚 相关文件

- `deps.mk` - 原始的依赖库编译配置 (macOS/Linux)
- `../ios-cmake/ios.toolchain.cmake` - iOS 工具链文件
- `../DEPS_BUILD.md` - 详细的使用文档
- `../build-librime-ios.sh` - librime iOS 编译脚本
