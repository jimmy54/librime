# 上下文排序测试指南

## 问题分析

### 当前问题
在console测试中，输入序列：
1. `yi` → 选"一"
2. `xin` → 选"心"  
3. `yi` → 选"一"
4. `yi` → 期望"意"，实际还是"一" ❌

### 根本原因

**上下文没有正确传递！**

#### 代码流程分析

1. **上下文获取**（`script_translator.cc:306-317`）
   ```cpp
   string ScriptTranslator::GetPrecedingText(size_t start) const {
     // 优先级1: 外部上下文（API设置的）
     const auto& external = engine_->context()->external_preceding_text();
     if (!external.empty()) {
       return external;
     }
     // 优先级2: 内部上下文（composition或commit_history）
     return start > 0 ? engine_->context()->composition().GetTextBefore(start)
                      : engine_->context()->commit_history().latest_text();
   }
   ```

2. **ContextualTranslation创建**（`poet.h:51`）
   ```cpp
   auto preceding_text = translator->GetPrecedingText(start);
   if (preceding_text.empty()) {
     return translation;  // ← 上下文为空，不进行排序！
   }
   return New<ContextualTranslation>(translation, input, preceding_text, grammar_.get());
   ```

3. **commit_history更新时机**（`engine.cc:254`）
   ```cpp
   void ConcreteEngine::OnCommit(Context* ctx) {
     context_->commit_history().Push(ctx->composition(), ctx->input());
     // ← 只有在composition提交时才更新
   }
   ```

#### 问题所在

在console测试中：
- ❌ **没有设置外部上下文**：`external_preceding_text_` 为空
- ❌ **每次输入都清空composition**：`simulate_key_sequence`会重置状态
- ❌ **commit_history没有更新**：因为没有真正的commit操作

结果：**每次GetPrecedingText()都返回空字符串** → 上下文排序不生效！

---

## 解决方案

### 方案1：手动设置外部上下文（推荐）

在console中，每次输入前手动设置上下文：

```bash
# 测试流程
yi                          # 输入yi
select candidate 1          # 选择候选1（一）
set context 一              # 手动设置上下文为"一"

xin                         # 输入xin
select candidate 1          # 选择候选1（心）
set context 一心            # 手动设置上下文为"一心"

yi                          # 输入yi
select candidate 2          # 选择候选2（一）
set context 一心一          # 手动设置上下文为"一心一"

yi                          # 再次输入yi
# 现在应该看到"意"排在前面！
```

### 方案2：修改console程序自动维护上下文

修改`rime_api_console.cc`，在每次commit后自动更新外部上下文：

```cpp
// 在print函数中添加
void print(RimeSessionId session_id) {
  RimeApi* rime = rime_get_api();
  
  // 获取commit
  RIME_STRUCT(RimeCommit, commit);
  if (rime->get_commit(session_id, &commit)) {
    printf("commit: %s\n", commit.text);
    
    // 自动更新上下文
    static string accumulated_text;
    accumulated_text += commit.text;
    
    // 保留最后3个字作为上下文
    if (accumulated_text.length() > 9) {  // 假设每个汉字3字节
      accumulated_text = accumulated_text.substr(accumulated_text.length() - 9);
    }
    
    if (RIME_API_AVAILABLE(rime, set_context_text)) {
      rime->set_context_text(session_id, accumulated_text.c_str(), "");
      printf("✓ Auto context: \"%s\"\n", accumulated_text.c_str());
    }
    
    rime->free_commit(&commit);
  }
  // ... 其他代码
}
```

### 方案3：使用真实的commit流程

不使用`simulate_key_sequence`，而是：
1. 逐个按键输入
2. 选择候选词
3. 按回车提交（触发真正的commit）

这样`commit_history`会自动更新。

---

## 验证方法

### 测试命令序列

```bash
# 启动console
./build/bin/rime_api_console

# 测试1：手动设置上下文
set context 一心一
yi
# 查看候选词，"意"应该排在前面

# 测试2：使用test命令
test context ranking
# 会自动运行预设的测试用例

# 测试3：查看日志
# 日志位置：./user_profile/log/rime.console.*.log.INFO.*
# 搜索关键词：contextual suggestion
```

### 预期日志输出

如果上下文排序生效，应该看到类似日志：

```
contextual suggestion: 意 weight: 15.234
contextual suggestion: 一 weight: 12.456
contextual suggestion: 亿 weight: 10.123
```

如果没有生效，会看到：

```
# 没有"contextual suggestion"日志
# 或者所有候选词的weight都是原始值
```

---

## 调试技巧

### 1. 启用详细日志

在console中设置：
```cpp
traits.min_log_level = 0;  // INFO级别
```

### 2. 查看上下文获取

在`script_translator.cc:306`添加日志：
```cpp
string ScriptTranslator::GetPrecedingText(size_t start) const {
  auto external = engine_->context()->external_preceding_text();
  auto internal = start > 0 ? engine_->context()->composition().GetTextBefore(start)
                            : engine_->context()->commit_history().latest_text();
  
  LOG(INFO) << "GetPrecedingText: external=\"" << external 
            << "\", internal=\"" << internal << "\"";
  
  // ... 原有逻辑
}
```

### 3. 查看ContextualTranslation是否创建

在`poet.h:51`添加日志：
```cpp
auto preceding_text = translator->GetPrecedingText(start);
LOG(INFO) << "ContextualWeighted: preceding_text=\"" << preceding_text << "\"";
if (preceding_text.empty()) {
  LOG(WARNING) << "No preceding text, skipping contextual ranking!";
  return translation;
}
```

---

## 总结

**问题核心**：console测试中没有正确维护上下文，导致`GetPrecedingText()`返回空字符串。

**解决方法**：
1. ✅ **推荐**：使用`set context`命令手动设置上下文
2. ✅ **自动化**：修改console程序自动维护上下文
3. ✅ **标准流程**：使用真实的commit流程触发commit_history更新

选择方案1最简单直接，适合快速测试！
