# librime 方案补丁系统 Bug 修复文档

## 问题描述

librime 的方案补丁（`.custom.yaml`）系统存在严重 bug，导致用户创建或修改 `.custom.yaml` 文件后，补丁内容无法被应用到编译后的方案文件中。

### 症状

1. 创建或修改 `xxx.custom.yaml` 文件后，运行 `rime_deployer --build` 不会触发重新编译
2. 即使强制删除 `build/xxx.schema.yaml` 文件，补丁内容仍然不会被应用
3. 编译后的 `build/xxx.schema.yaml` 文件中的 `__build_info/timestamps` 显示 `.custom` 文件的时间戳为 0
4. 用户配置的自定义内容（如快捷键、翻译器等）完全不生效

## 根本原因分析

经过深入分析源码和调试，发现问题由两个独立的 bug 共同导致：

### Bug 1: ConfigNeedsUpdate() 不检查新添加的 .custom.yaml 文件

**位置**: `src/rime/lever/deployment_tasks.cc` 中的 `ConfigNeedsUpdate()` 函数

**问题**:

当 `.custom.yaml` 文件在初次编译后才创建时，会出现以下循环依赖：

1. 初次编译时 `.custom.yaml` 不存在 → `BuildInfoPlugin` 不记录该文件（或记录时间戳为 0）
2. 用户创建 `.custom.yaml` 文件
3. 再次部署时，`ConfigNeedsUpdate()` 只检查 `__build_info/timestamps` 中已记录的文件
4. 由于 `.custom.yaml` 不在 timestamps 中（或时间戳为 0），系统认为文件不存在，跳过检查
5. 不触发重新编译 → `.custom.yaml` 永远不会被加载

**代码逻辑**:

```cpp
// 原有逻辑
for (auto entry : *timestamps.AsMap()) {
  // ...
  if (!fs::exists(source_file)) {
    if (recorded_time) {
      return true;  // 文件被删除，触发更新
    }
    continue;  // recorded_time 为 0，跳过检查 ← 问题所在
  }
  // 检查时间戳变化
}
return false;  // 没有检测到变化，不更新
```

当 `recorded_time` 为 0 且文件不存在时，代码会 `continue` 跳过检查。但是如果文件后来被创建了，这个检查逻辑无法检测到。

### Bug 2: AutoPatchConfigPlugin 跳过有显式 __patch 的 schema

**位置**: `src/rime/config/auto_patch_config_plugin.cc` 中的 `ReviewCompileOutput()` 函数

**问题**:

`AutoPatchConfigPlugin` 的设计目标是自动为每个 schema 添加对其对应 `.custom.yaml` 文件的引用。但是它有一个逻辑：

```cpp
// 原有逻辑
auto root_deps = compiler->GetDependencies(resource->resource_id + ":");
if (!root_deps.empty() && root_deps.back()->priority() >= kPatch)
  return true;  // 跳过已有 __patch 的 schema
```

这个逻辑假设：如果 schema 文件中已经有 `__patch` 节点，说明用户已经手动处理了补丁，不需要自动添加。

**实际情况**:

许多 schema 文件（如 `rime_ice_26.schema.yaml`）使用 `__patch` 节点来配置其他功能（如语法检查、上下文建议等）：

```yaml
__patch:
  grammar:
    language: wanxiang-lts-zh-hans
    collocation_max_length: 8
  translator/contextual_suggestions: true
  translator/contextual_group_by_type: false
```

这些 `__patch` 节点与 `.custom.yaml` 文件无关，但 `AutoPatchConfigPlugin` 会误认为用户已经处理了补丁，从而跳过自动添加对 `.custom.yaml` 的引用。

**结果**: 即使 `.custom.yaml` 文件存在，也不会被加载和应用。

## 修复方案

### 修复 1: 增强 ConfigNeedsUpdate() 检测逻辑

在 `src/rime/lever/deployment_tasks.cc` 的 `ConfigNeedsUpdate()` 函数末尾添加额外检查：

```cpp
// 检查可能新添加的 .custom.yaml 文件
// 遍历所有 .schema 资源，检查对应的 .custom 文件是否存在但未记录
for (auto entry : *timestamps.AsMap()) {
  string resource_id = entry.first;
  if (boost::ends_with(resource_id, ".schema")) {
    // 构造对应的 .custom 资源 ID
    string custom_id = resource_id.substr(0, resource_id.length() - 7) + ".custom";
    
    // 检查 .custom 是否在 timestamps 中
    bool custom_in_timestamps = false;
    for (auto ts_entry : *timestamps.AsMap()) {
      if (ts_entry.first == custom_id) {
        custom_in_timestamps = true;
        break;
      }
    }
    
    // 如果不在 timestamps 中，检查文件是否存在
    if (!custom_in_timestamps) {
      path custom_file = resolver->ResolvePath(custom_id);
      if (fs::exists(custom_file)) {
        LOG(INFO) << "found new custom file: " << custom_file;
        return true;  // 触发重新编译
      }
    }
  }
}
```

**效果**: 即使 `.custom.yaml` 文件不在 timestamps 记录中，只要文件存在，就会触发重新编译。

### 修复 2: 改进 AutoPatchConfigPlugin 判断逻辑

在 `src/rime/config/auto_patch_config_plugin.cc` 的 `ReviewCompileOutput()` 函数中，不再简单地跳过有 `__patch` 的 schema，而是检查 `.custom` 是否已经被引用：

```cpp
bool AutoPatchConfigPlugin::ReviewCompileOutput(ConfigCompiler* compiler,
                                                an<ConfigResource> resource) {
  if (boost::ends_with(resource->resource_id, ".custom"))
    return true;
  
  auto patch_resource_id =
      remove_suffix(resource->resource_id, ".schema") + ".custom";
  
  // 检查 .custom 补丁是否已经被显式引用
  auto root_deps = compiler->GetDependencies(resource->resource_id + ":");
  for (const auto& dep : root_deps) {
    if (dep->repr().find(patch_resource_id) != string::npos) {
      // .custom 已经被引用，不需要重复添加
      return true;
    }
  }
  
  // 如果没有引用，自动添加
  LOG(INFO) << "auto-patch " << resource->resource_id
            << ":/__patch: " << patch_resource_id << ":/patch?";
  compiler->Push(resource);
  compiler->AddDependency(
      New<PatchReference>(Reference{patch_resource_id, "patch", true}));
  compiler->Pop();
  return true;
}
```

**效果**: 
- 即使 schema 有显式的 `__patch` 节点，只要没有引用对应的 `.custom` 文件，就会自动添加引用
- 避免重复引用（如果用户已经手动添加了引用）

## 验证结果

修复后的行为：

### 场景 1: 新创建 .custom.yaml 文件

```bash
# 1. 初次部署（没有 .custom.yaml）
$ rime_deployer --build user_data_dir

# 2. 创建 .custom.yaml 文件
$ cat > user_data_dir/rime_ice.custom.yaml << EOF
patch:
  date_translator:
    date: rr
EOF

# 3. 再次部署
$ rime_deployer --build user_data_dir
# ✅ 系统检测到新文件，触发重新编译
# ✅ 补丁内容被正确应用
```

### 场景 2: 修改现有 .custom.yaml 文件

```bash
# 修改 .custom.yaml 文件
$ echo "  time: sj" >> user_data_dir/rime_ice.custom.yaml

# 部署
$ rime_deployer --build user_data_dir
# ✅ 系统检测到文件时间戳变化，触发重新编译
# ✅ 新的补丁内容被应用
```

### 场景 3: schema 有显式 __patch 节点

对于包含以下内容的 schema：

```yaml
__patch:
  grammar:
    language: wanxiang-lts-zh-hans
  translator/contextual_suggestions: true
```

```bash
$ rime_deployer --build user_data_dir
# ✅ AutoPatchConfigPlugin 检测到没有引用 .custom 文件
# ✅ 自动添加对 .custom 文件的引用
# ✅ 补丁内容被正确应用
```

### 验证编译结果

```bash
# 检查编译后的文件
$ cat build/rime_ice.schema.yaml | head -10
__build_info:
  rime_version: 1.14.0
  timestamps:
    rime_ice.schema: 1761248732
    rime_ice.custom: 1761247165  # ✅ 时间戳正确记录
    
# 检查补丁是否应用
$ cat build/rime_ice.schema.yaml | grep "date:"
  date: rr  # ✅ 从 .custom.yaml 应用的值
```

## 影响范围

### 受影响的版本

- librime 1.14.0 及之前的所有版本
- 所有使用 `.custom.yaml` 文件进行方案自定义的用户

### 受影响的场景

1. **新用户配置**: 用户首次创建 `.custom.yaml` 文件时，补丁不会生效
2. **方案迁移**: 从其他系统迁移配置时，`.custom.yaml` 文件可能不会被识别
3. **复杂 schema**: 使用了显式 `__patch` 节点的 schema（如 rime-ice 等）完全无法使用 `.custom.yaml`
4. **CI/CD 环境**: 自动化构建流程中，补丁可能不会被应用

## 技术细节

### 配置编译流程

librime 的配置编译流程：

```
1. SchemaUpdate::Run()
   ↓
2. ConfigFileUpdate::Run()
   ↓
3. ConfigNeedsUpdate() 检查是否需要更新
   ↓ (如果需要)
4. ConfigBuilder::LoadConfig()
   ↓
5. ConfigCompiler::Compile()
   ↓
6. AutoPatchConfigPlugin::ReviewCompileOutput()
   ↓ (添加 .custom 引用)
7. ConfigCompiler::Link()
   ↓
8. PatchReference::Resolve()
   ↓ (加载 .custom 文件)
9. BuildInfoPlugin::ReviewLinkOutput()
   ↓ (记录时间戳)
10. SaveOutputPlugin::ReviewLinkOutput()
```

### 关键数据结构

**__build_info 结构**:

```yaml
__build_info:
  rime_version: "1.14.0"
  timestamps:
    schema_name.schema: 1761248732    # schema 文件时间戳
    schema_name.custom: 1761247165    # custom 文件时间戳
    default: 1761241983               # 依赖文件时间戳
    default.custom: 0                 # 不存在的文件记录为 0
```

**依赖关系**:

```
schema.yaml
  ├─ __patch: (显式)
  │    ├─ grammar: {...}
  │    └─ translator/xxx: {...}
  └─ __patch: (自动添加)
       └─ schema.custom:/patch? (可选引用)
            └─ patch:
                 ├─ key1: value1
                 └─ key2: value2
```

## 相关文件

### 修改的文件

1. `src/rime/lever/deployment_tasks.cc`
   - 函数: `ConfigNeedsUpdate()`
   - 修改: 添加新文件检测逻辑

2. `src/rime/config/auto_patch_config_plugin.cc`
   - 函数: `ReviewCompileOutput()`
   - 修改: 改进 __patch 检测逻辑

### 相关文件

- `src/rime/config/config_compiler.cc` - 配置编译器核心逻辑
- `src/rime/config/build_info_plugin.cc` - 构建信息和时间戳记录
- `src/rime/lever/customizer.cc` - 旧版补丁系统（已废弃）
- `src/rime/lever/custom_settings.cc` - 自定义设置管理

## 测试建议

### 单元测试

```cpp
// 测试 ConfigNeedsUpdate 检测新文件
TEST(DeploymentTasksTest, ConfigNeedsUpdateDetectsNewCustomFile) {
  // 1. 创建 schema 文件
  // 2. 初次编译
  // 3. 创建 .custom.yaml 文件
  // 4. 验证 ConfigNeedsUpdate() 返回 true
}

// 测试 AutoPatchConfigPlugin 处理显式 __patch
TEST(AutoPatchConfigPluginTest, HandlesExplicitPatch) {
  // 1. 创建包含 __patch 的 schema
  // 2. 编译
  // 3. 验证 .custom 引用被添加
}
```

### 集成测试

```bash
#!/bin/bash
# 测试脚本

# 1. 清理环境
rm -rf test_data/build

# 2. 初次部署（无 .custom 文件）
rime_deployer --build test_data

# 3. 创建 .custom.yaml
cat > test_data/test.custom.yaml << EOF
patch:
  test_key: test_value
EOF

# 4. 再次部署
rime_deployer --build test_data

# 5. 验证补丁应用
if grep -q "test_value" test_data/build/test.schema.yaml; then
  echo "✅ Test passed"
else
  echo "❌ Test failed"
  exit 1
fi
```

## 向后兼容性

### 兼容性说明

- ✅ **完全向后兼容**: 修复不会影响现有的正常工作的配置
- ✅ **无需用户操作**: 用户不需要修改任何配置文件
- ✅ **渐进式改进**: 只修复有问题的场景，不改变正常场景的行为

### 迁移指南

对于已经受此 bug 影响的用户：

1. **更新 librime**: 升级到包含此修复的版本
2. **重新部署**: 运行 `rime_deployer --build`
3. **验证**: 检查 `.custom.yaml` 的内容是否生效

不需要：
- ❌ 修改 schema 文件
- ❌ 删除或重建用户数据
- ❌ 修改 `.custom.yaml` 文件格式

## 参考资料

### 相关 Issue

- 本次修复解决的问题：方案补丁不起作用

### 相关文档

- [RIME 定制指南](https://github.com/rime/home/wiki/CustomizationGuide)
- [配置文件说明](https://github.com/rime/home/wiki/Configuration)
- [补丁系统设计](https://github.com/rime/home/wiki/RimeWithSchemata#patch)

### 代码审查

- 修改遵循 RIME 编码规范
- 添加了详细的注释和日志
- 保持了与现有代码的一致性

## 总结

这是一个由两个独立 bug 共同导致的严重问题：

1. **检测缺陷**: 系统无法检测到新添加的 `.custom.yaml` 文件
2. **逻辑缺陷**: 自动补丁插件错误地跳过了需要处理的 schema

修复后，librime 的方案补丁系统能够：
- ✅ 正确检测新创建的 `.custom.yaml` 文件
- ✅ 正确处理包含显式 `__patch` 的 schema
- ✅ 准确记录文件时间戳
- ✅ 可靠地应用用户自定义配置

这个修复对所有使用 `.custom.yaml` 进行方案自定义的用户都至关重要。

---

**修复日期**: 2025-10-24  
**修复版本**: librime 1.14.0+  
**文档版本**: 1.0
