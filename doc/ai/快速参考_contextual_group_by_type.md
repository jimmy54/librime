# contextual_group_by_type 快速参考

## 🚀 一分钟快速上手

### 推荐配置（复制即用）

```yaml
# wanxiang.schema.yaml
translator:
  contextual_suggestions: true
  contextual_group_by_type: false     # 推荐：完全按权重排序
```

### 效果

```
输入: "jia" → "家"
输入: "ting"

结果:
1. [庭]  ← "家庭"搭配，排第一 ✅
2.  听
3.  挺
```

---

## 📊 配置对比

| 配置值 | 排序方式 | 适合人群 |
|--------|---------|---------|
| `false` | 完全按权重 | 追求最佳体验 ⭐⭐⭐⭐⭐ |
| `true` | 按类型分组 | 保守用户 ⭐⭐⭐ |

---

## 🔧 代码修改清单

### 修改的文件（5个）

1. ✅ `src/rime/gear/contextual_translation.h`
2. ✅ `src/rime/gear/contextual_translation.cc`
3. ✅ `src/rime/gear/translator_commons.h`
4. ✅ `src/rime/gear/translator_commons.cc`
5. ✅ `src/rime/gear/poet.h`

### 编译命令

```bash
cd /Users/jimmy54/Pictures/jimmy_librime/librime
make clean
make
make clang-format-apply  # 代码格式化
```

---

## 🧪 测试命令

```bash
# 重新部署
./rime_deployer --build user_profile

# 测试
./rime_console
# 输入 "jia" → "家"
# 输入 "ting"
# 期望：庭 排第一
```

---

## 📝 Git Commit

```bash
git commit -m "feat: add contextual_group_by_type config option

- Add configurable grouping behavior in ContextualTranslation
- Default to false for better contextual ranking
- Users can set to true to maintain legacy behavior
"
```

---

## ❓ 常见问题

**Q: 默认值是什么？**
A: `false`（不按类型分组）

**Q: 会影响性能吗？**
A: 不会（< 1ms 差异）

**Q: 如何恢复旧行为？**
A: 设置 `contextual_group_by_type: true`

---

## 📚 详细文档

- 配置说明：`contextual_group_by_type配置说明.md`
- 代码修改：`代码修改总结_contextual_group_by_type.md`
- 源码分析：`ContextualTranslation源码分析与修改影响评估.md`
