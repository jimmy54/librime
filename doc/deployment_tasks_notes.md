# `deployment_tasks.cc` 学习笔记

> 位置：`src/rime/lever/deployment_tasks.cc`
>
> 作用：为 Rime 输入法执行部署与维护相关的自动任务（配置刷新、词典编译、备份清理等）。

---

## 1. 文件结构概览

- **核心概念**：每个任务继承 `DeploymentTask`，实现 `Run(Deployer* deployer)`。
- **主要任务**：
  - **检测**：`DetectModifications`
  - **安装信息**：`InstallationUpdate`
  - **工作区维护**：`WorkspaceUpdate`
  - **配置/词典**：`SchemaUpdate`、`ConfigFileUpdate`、`PrebuildAllSchemas`
  - **字典/同步**：`SymlinkingPrebuiltDictionaries`、`UserDictUpgrade`、`UserDictSync`
  - **备份/清理**：`BackupConfigFiles`、`CleanupTrash`、`CleanOldLogFiles`

---

## 2. 检测与更新任务

### 2.1 `DetectModifications`

```cpp
bool DetectModifications::Run(Deployer* deployer) {
  time_t last_modified = 0;
  // 遍历目录记录最后修改时间
  // ...
  if (last_modified > (time_t)last_build_time) {
    LOG(INFO) << "modifications detected. workspace needs update.";
    return true;
  }
  return false;
}
```

- **角色**：侦察兵，检查配置文件是否比 `user_config` 中的 `var/last_build_time` 更晚。
- **类比**：像定期检查冰箱是否有新食材，判断是否需要重新做饭。

### 2.2 `InstallationUpdate`

```cpp
bool InstallationUpdate::Run(Deployer* deployer) {
  // 读取/创建 installation.yaml 并更新安装信息
  if (!installation_id.empty() &&
      last_distro_code_name == deployer->distribution_code_name &&
      last_distro_version == deployer->distribution_version &&
      last_rime_version == RIME_VERSION) {
    return true;
  }
  // 生成新的安装信息并写回文件
  return config.SaveToFile(installation_info);
}
```

- **角色**：档案管理员，维护 `installation.yaml` 中的安装 ID、同步目录、版本信息。
- **类比**：像更新手机设备档案，记录系统版本和安装时间。

### 2.3 `WorkspaceUpdate`

```cpp
bool WorkspaceUpdate::Run(Deployer* deployer) {
  the<DeploymentTask> t;
  t.reset(new ConfigFileUpdate("default.yaml", "config_version"));
  t->Run(deployer);
  t.reset(new SymlinkingPrebuiltDictionaries);
  t->Run(deployer);

  // 遍历 default.yaml 的 schema_list 逐一更新方案
  // ...

  user_config->SetInt("var/last_build_time", (int)time(NULL));
  return failure == 0;
}
```

- **角色**：总指挥，协调多个子任务更新工作区。
- **流程**：刷新公共配置 → 更新每个方案及依赖 → 记录完成时间。

---

## 3. 词典与配置构建

### 3.1 `SchemaUpdate`

```cpp
bool SchemaUpdate::Run(Deployer* deployer) {
  // 1. 读取 schema/schema_id
  // 2. 调用 ConfigFileUpdate 更新编译配置
  // 3. 使用 DictCompiler 编译词典
  if (!dict_compiler.Compile(compiled_schema)) {
    LOG(ERROR) << "dictionary '" << dict_name << "' failed to compile.";
    return false;
  }
  return true;
}
```

- **角色**：厨师，按 `.schema.yaml` 菜谱烹饪词典。
- **要点**：若方案不依赖词典，直接返回；否则编译字典输出到 `staging_dir`。

### 3.2 `ConfigFileUpdate`

```cpp
bool ConfigFileUpdate::Run(Deployer* deployer) {
  // 1. 老版本用户拷贝移入 trash
  if (TrashDeprecatedUserCopy(...)) {
    // ...
  }
  // 2. 判断 __build_info/timestamps 是否过期
  the<Config> config(Config::Require("config")->Create(file_name_));
  if (ConfigNeedsUpdate(config.get())) {
    config.reset(Config::Require("config_builder")->Create(file_name_));
  }
  return true;
}
```

- **角色**：文档管家，确保配置文件保持最新版本。
- **辅助函数**：
  - `ConfigNeedsUpdate()`：检查源文件时间戳是否匹配。
  - `TrashDeprecatedUserCopy()`：把老旧版本拷贝移到 `trash`。

### 3.3 `PrebuildAllSchemas`

- **作用**：遍历共享目录所有 `.schema.yaml`，批量运行 `SchemaUpdate`。
- **类比**：像一次性备齐厨房所有菜谱所需食材。

### 3.4 `SymlinkingPrebuiltDictionaries`

- **作用**：清理用户目录中旧版本创建的符号链接，避免指向已删除的共享词典。
- **类比**：拆掉失效的标签，防止指错抽屉。

---

## 4. 用户词典与同步

### 4.1 `UserDictUpgrade`

```cpp
bool UserDictUpgrade::Run(Deployer* deployer) {
  LoadModules(kLegacyModules);
  auto legacy_userdb_component = UserDb::Require("legacy_userdb");
  if (!legacy_userdb_component) {
    return true;
  }
  UserDictManager manager(deployer);
  // 升级每个旧版词典
  // ...
}
```

- **作用**：将旧版 `legacy_userdb` 用户词典迁移到新版格式。
- **类比**：把旧手机联系人导入新手机。

### 4.2 `UserDictSync`

```cpp
bool UserDictSync::Run(Deployer* deployer) {
  UserDictManager mgr(deployer);
  return mgr.SynchronizeAll();
}
```

- **作用**：调用 `UserDictManager::SynchronizeAll()`，确保词典与同步目录一致。
- **类比**：像把作业同步到云盘，避免丢失。

---

## 5. 备份与清理

### 5.1 `BackupConfigFiles`

```cpp
bool BackupConfigFiles::Run(Deployer* deployer) {
  if (!deployer->backup_config_files) {
    return true;
  }
  // 遍历用户目录，将 .yaml/.txt 复制到同步目录
  // 对带 customization 的 .yaml 保持跳过
}
```

- **作用**：备份用户配置文件到同步目录。
- **注意**：带 `customization` 字段（用户自定义）的 `.yaml` 不覆盖，以免丢失手动修改。

### 5.2 `CleanupTrash`

```cpp
bool CleanupTrash::Run(Deployer* deployer) {
  // 将 rime.log、*.bin、*.reverse.kct 等文件移到 trash/
  // ...
}
```

- **作用**：把无用日志、缓存移入 `trash` 目录。
- **类比**：定期把废纸箱搬到储物间。

### 5.3 `CleanOldLogFiles`

```cpp
bool CleanOldLogFiles::Run(Deployer* deployer) {
#ifdef RIME_ENABLE_LOGGING
  if (FLAGS_logtostderr || FLAGS_log_dir.empty()) {
    return true;
  }
  // 删除非当天的 .log 文件
#endif
  return success;
}
```

- **作用**：在启用 `glog` 的情况下清扫旧日志。
- **类比**：每天睡前把昨天以前的日记草稿扔掉。

---

## 6. 常用辅助函数

| 函数 | 用途 |
|------|------|
| `MaybeCreateDirectory(path dir)` | 确保目录存在，失败则记录错误。 |
| `RemoveVersionSuffix(string* version, const string& suffix)` | 去掉版本号的特殊尾缀（如 `.minimal`）。 |
| `TrashDeprecatedUserCopy(...)` | 比较版本后将老旧用户配置移动到 `trash`。 |
| `ConfigNeedsUpdate(Config* config)` | 根据 `__build_info/timestamps` 判定是否需要重编译配置。 |

---

## 7. 生活化记忆

- **DetectModifications**：冰箱检查员。
- **InstallationUpdate**：设备档案管理员。
- **WorkspaceUpdate**：大扫除总指挥。
- **SchemaUpdate**：菜谱厨师。
- **ConfigFileUpdate**：作业模板管理员。
- **BackupConfigFiles**：资料复印员。
- **CleanupTrash / CleanOldLogFiles**：垃圾清运员。

---

## 8. 深入学习建议

- 进一步查看 `rime/lever/deployment_tasks.h`，了解基类 `DeploymentTask` 的声明与任务注册方式。
- 想研究词典编译细节，可阅读 `rime/dict/dict_compiler.cc` 与 `rime/dict/dictionary.h`。
- 想掌握同步与备份实现，可延伸到 `rime/lever/user_dict_manager.cc`。

---

> 若在项目中继续迭代，请在 `README.md` 或 `doc/` 目录补充学习心得与改进建议，方便团队和未来的自己快速上手。
