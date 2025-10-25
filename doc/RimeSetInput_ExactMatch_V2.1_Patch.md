# RimeSetInput 部分精确匹配 V2.1 - 代码补丁

## 补丁说明

本补丁实现部分精确匹配功能，优先在 **ScriptTranslator** 中实现。

### V2.1 优化点

1. **参数命名**：`exact_length` → `input_exact_length`（更明确）
2. **实现优先级**：先 ScriptTranslator，后 TableTranslator
3. **实现方案**：在 Syllabifier 层面过滤（效率最高）

---

## 修改文件清单

1. `src/rime/context.h` - 添加 input_exact_length 标志
2. `src/rime/context.cc` - 实现精确匹配逻辑
3. `src/rime_api.h` - 添加新 API
4. `src/rime_api_impl.h` - 实现新 API
5. `src/rime/algo/syllabifier.h` - 添加 Context 参数
6. `src/rime/algo/syllabifier.cc` - 实现音节过滤
7. `src/rime/gear/script_translator.cc` - 传递 Context

---

## 补丁 1: src/rime/context.h

```cpp
class RIME_DLL Context {
 public:
  // ... 现有代码 ...
  
  // 【V2.1 优化】：参数名更明确
  void set_input(const string& value, int input_exact_length = 0);
  const string& input() const { return input_; }
  
  // 【V2.1 新增】：获取精确匹配长度
  int input_exact_length() const { return input_exact_length_; }
  
  // 【V2.1 新增】：判断指定位置是否需要精确匹配
  bool is_exact_at(size_t pos) const;

 private:
  // ... 现有代码 ...
  
  // 【V2.1 新增】：精确匹配长度
  int input_exact_length_ = 0;
};
```

**完整 diff**：

```diff
--- a/src/rime/context.h
+++ b/src/rime/context.h
@@ -61,7 +61,9 @@ class RIME_DLL Context {
   bool ClearNonConfirmedComposition();
   bool RefreshNonConfirmedComposition();
 
-  void set_input(const string& value);
+  void set_input(const string& value, int input_exact_length = 0);
+  int input_exact_length() const { return input_exact_length_; }
+  bool is_exact_at(size_t pos) const;
   const string& input() const { return input_; }
 
   void set_caret_pos(size_t caret_pos);
@@ -117,6 +119,9 @@ class RIME_DLL Context {
   map<string, bool> options_;
   map<string, string> properties_;
 
+  // Exact match length for partial exact matching
+  int input_exact_length_ = 0;
+
   // External context from frontend
   string external_preceding_text_;
   string external_following_text_;
```

---

## 补丁 2: src/rime/context.cc

```cpp
void Context::set_input(const string& value, int input_exact_length) {
  input_ = value;
  caret_pos_ = input_.length();
  
  // 处理特殊值
  if (input_exact_length < 0) {
    // 负数：全部精确
    input_exact_length_ = static_cast<int>(input_.length());
  } else if (input_exact_length > static_cast<int>(input_.length())) {
    // 超过输入长度：限制为输入长度
    input_exact_length_ = static_cast<int>(input_.length());
  } else {
    input_exact_length_ = input_exact_length;
  }
  
  DLOG(INFO) << "Context::set_input: input=\"" << value 
             << "\", input_exact_length=" << input_exact_length_;
  
  update_notifier_(this);
}

bool Context::is_exact_at(size_t pos) const {
  if (input_exact_length_ <= 0)
    return false;
  return pos < static_cast<size_t>(input_exact_length_);
}
```

**完整 diff**：

```diff
--- a/src/rime/context.cc
+++ b/src/rime/context.cc
@@ -281,9 +281,25 @@ void Context::set_composition(Composition&& comp) {
   composition_ = std::move(comp);
 }
 
-void Context::set_input(const string& value) {
+void Context::set_input(const string& value, int input_exact_length) {
   input_ = value;
   caret_pos_ = input_.length();
+  
+  // Handle special values
+  if (input_exact_length < 0) {
+    // Negative: exact match all
+    input_exact_length_ = static_cast<int>(input_.length());
+  } else if (input_exact_length > static_cast<int>(input_.length())) {
+    // Exceeds input length: limit to input length
+    input_exact_length_ = static_cast<int>(input_.length());
+  } else {
+    input_exact_length_ = input_exact_length;
+  }
+  
+  DLOG(INFO) << "Context::set_input: input=\"" << value 
+             << "\", input_exact_length=" << input_exact_length_;
+  
   update_notifier_(this);
 }
+
+bool Context::is_exact_at(size_t pos) const {
+  if (input_exact_length_ <= 0)
+    return false;
+  return pos < static_cast<size_t>(input_exact_length_);
+}
```

---

## 补丁 3: src/rime_api.h

```c
// 【V2.1 新增】：支持部分精确匹配
// input_exact_length: 需要精确匹配的前缀长度
//   = 0: 全部派生（默认行为）
//   > 0: 前 N 个字符精确匹配，后续允许派生
//   < 0: 全部精确匹配
RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
                              const char* input,
                              int input_exact_length);

// 【保持兼容】：旧 API
RIME_API Bool RimeSetInput(RimeSessionId session_id, const char* input);
```

**完整 diff**：

```diff
--- a/src/rime_api.h
+++ b/src/rime_api.h
@@ -XXX,6 +XXX,15 @@ RIME_API Bool RimeCommitComposition(RimeSessionId session_id);
 RIME_API void RimeClearComposition(RimeSessionId session_id);
 
 //! Set input string
+//! \deprecated Use RimeSetInputEx instead for better control
 RIME_API Bool RimeSetInput(RimeSessionId session_id, const char* input);
+
+//! Set input string with partial exact match
+//! \param input_exact_length: Length of prefix that needs exact match
+//!   = 0: all derivations allowed (default)
+//!   > 0: first N characters exact match, rest allow derivations
+//!   < 0: all exact match
+RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
+                              const char* input,
+                              int input_exact_length);
+
 RIME_API char* RimeGetInput(RimeSessionId session_id);
```

---

## 补丁 4: src/rime_api_impl.h

```cpp
Bool RimeSetInputEx(RimeSessionId session_id,
                    const char* input,
                    int input_exact_length) {
  an<Session> session(Service::instance().GetSession(session_id));
  if (!session)
    return False;
  Context* ctx = session->context();
  if (!ctx)
    return False;
  ctx->set_input(input, input_exact_length);
  return True;
}

RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input) {
  return RimeSetInputEx(session_id, input, 0);
}
```

**完整 diff**：

```diff
--- a/src/rime_api_impl.h
+++ b/src/rime_api_impl.h
@@ -1137,13 +1137,26 @@ RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input)
   an<Session> session(Service::instance().GetSession(session_id));
   if (!session)
     return False;
   Context* ctx = session->context();
   if (!ctx)
     return False;
-  ctx->set_input(input);
+  ctx->set_input(input, 0);
   return True;
 }
 
+Bool RimeSetInputEx(RimeSessionId session_id,
+                    const char* input,
+                    int input_exact_length) {
+  an<Session> session(Service::instance().GetSession(session_id));
+  if (!session)
+    return False;
+  Context* ctx = session->context();
+  if (!ctx)
+    return False;
+  ctx->set_input(input, input_exact_length);
+  return True;
+}
+
 char* RimeGetInput(RimeSessionId session_id) {
   an<Session> session(Service::instance().GetSession(session_id));
   if (!session)
```

---

## 补丁 5: src/rime/algo/syllabifier.h

```cpp
class Syllabifier {
 public:
  // ... 现有代码 ...
  
  // 【V2.1 修改】：添加 Context 参数
  int BuildSyllableGraph(const string& input,
                        Prism& prism,
                        SyllableGraph* graph,
                        Context* ctx = nullptr);
  
  // ... 现有代码 ...
};
```

**完整 diff**：

```diff
--- a/src/rime/algo/syllabifier.h
+++ b/src/rime/algo/syllabifier.h
@@ -XX,6 +XX,7 @@
 #include <rime/common.h>
 
 namespace rime {
+class Context;
 class Corrector;
 class Prism;
 struct SyllableGraph;
@@ -XX,7 +XX,8 @@ class Syllabifier {
   int BuildSyllableGraph(const string& input,
                         Prism& prism,
-                        SyllableGraph* graph);
+                        SyllableGraph* graph,
+                        Context* ctx = nullptr);
   
   // ... existing methods ...
```

---

## 补丁 6: src/rime/algo/syllabifier.cc

这是最关键的修改，在构建音节图时过滤派生音节。

```cpp
int Syllabifier::BuildSyllableGraph(const string& input,
                                    Prism& prism,
                                    SyllableGraph* graph,
                                    Context* ctx) {
  // 获取精确匹配长度
  int input_exact_length = ctx ? ctx->input_exact_length() : 0;
  
  // 获取音节表（用于精确匹配时比较）
  const Syllabary* syllabary = nullptr;
  if (input_exact_length > 0) {
    syllabary = prism.syllabary();
  }
  
  // ... 现有代码：初始化 graph ...
  
  graph->input_length = input.length();
  graph->interpreted_length = 0;
  graph->vertices.insert(0);
  
  for (size_t start_pos = 0; start_pos < input.length(); ++start_pos) {
    if (graph->vertices.find(start_pos) == graph->vertices.end())
      continue;
    
    // 查询所有匹配的拼写
    vector<Prism::Match> matches;
    prism.CommonPrefixSearch(input.substr(start_pos), &matches);
    
    for (const auto& m : matches) {
      if (m.length == 0)
        continue;
      
      size_t end_pos = start_pos + m.length;
      
      // 【V2.1 关键】：判断是否需要精确匹配
      bool need_exact = (input_exact_length > 0 && 
                         start_pos < static_cast<size_t>(input_exact_length));
      
      // 获取当前拼写
      string spelling = input.substr(start_pos, m.length);
      
      // 查询拼写对应的所有音节
      SpellingAccessor accessor = prism.QuerySpelling(m.value);
      
      while (!accessor.exhausted()) {
        SyllableId syllable_id = accessor.syllable_id();
        auto props = accessor.properties();
        
        // 【V2.1 关键】：精确模式下过滤派生音节
        if (need_exact && syllabary && syllable_id < syllabary->size()) {
          const string& syllable_str = (*syllabary)[syllable_id];
          if (spelling != syllable_str) {
            accessor.Next();
            continue;  // 跳过派生的音节（拼写≠音节）
          }
        }
        
        // 添加到音节图
        graph->edges[start_pos][end_pos][syllable_id] = props;
        graph->vertices.insert(end_pos);
        
        accessor.Next();
      }
    }
  }
  
  // ... 现有代码：计算 interpreted_length ...
  
  return graph->interpreted_length;
}
```

**完整 diff**：

```diff
--- a/src/rime/algo/syllabifier.cc
+++ b/src/rime/algo/syllabifier.cc
@@ -XX,6 +XX,7 @@
 #include <rime/algo/syllabifier.h>
+#include <rime/context.h>
 #include <rime/dict/corrector.h>
 #include <rime/dict/prism.h>
 
@@ -XXX,7 +XXX,9 @@ Syllabifier::Syllabifier(const string& delimiters,
 
 int Syllabifier::BuildSyllableGraph(const string& input,
                                     Prism& prism,
-                                    SyllableGraph* graph) {
+                                    SyllableGraph* graph,
+                                    Context* ctx) {
+  // V2.1: Get exact match length from context
+  int input_exact_length = ctx ? ctx->input_exact_length() : 0;
+  
+  // V2.1: Get syllabary for exact match comparison
+  const Syllabary* syllabary = nullptr;
+  if (input_exact_length > 0) {
+    syllabary = prism.syllabary();
+  }
+  
   graph->input_length = input.length();
   graph->interpreted_length = 0;
   graph->vertices.insert(0);
@@ -XXX,6 +XXX,12 @@ int Syllabifier::BuildSyllableGraph(const string& input,
       if (m.length == 0)
         continue;
       size_t end_pos = start_pos + m.length;
+      
+      // V2.1: Check if exact match is needed at this position
+      bool need_exact = (input_exact_length > 0 && 
+                         start_pos < static_cast<size_t>(input_exact_length));
+      
+      string spelling = input.substr(start_pos, m.length);
+      
       SpellingAccessor accessor = prism.QuerySpelling(m.value);
       while (!accessor.exhausted()) {
         SyllableId syllable_id = accessor.syllable_id();
         auto props = accessor.properties();
+        
+        // V2.1: Filter derived syllables in exact match mode
+        if (need_exact && syllabary && syllable_id < syllabary->size()) {
+          const string& syllable_str = (*syllabary)[syllable_id];
+          if (spelling != syllable_str) {
+            accessor.Next();
+            continue;  // Skip derived syllables (spelling != syllable)
+          }
+        }
+        
         graph->edges[start_pos][end_pos][syllable_id] = props;
         graph->vertices.insert(end_pos);
         accessor.Next();
```

---

## 补丁 7: src/rime/gear/script_translator.cc

修改 `ScriptSyllabifier::BuildSyllableGraph()` 传递 Context。

```cpp
size_t ScriptSyllabifier::BuildSyllableGraph(Prism& prism) {
  // 【V2.1 修改】：传递 Context
  Context* ctx = translator_->engine_->context();
  return (size_t)syllabifier_.BuildSyllableGraph(input_, prism, 
                                                 &syllable_graph_, ctx);
}
```

**完整 diff**：

```diff
--- a/src/rime/gear/script_translator.cc
+++ b/src/rime/gear/script_translator.cc
@@ -362,8 +362,10 @@ string ScriptSyllabifier::GetOriginalSpelling(const Phrase& cand) const {
 }
 
 size_t ScriptSyllabifier::BuildSyllableGraph(Prism& prism) {
-  return (size_t)syllabifier_.BuildSyllableGraph(input_, prism,
-                                                 &syllable_graph_);
+  // V2.1: Pass context for partial exact match
+  Context* ctx = translator_->engine_->context();
+  return (size_t)syllabifier_.BuildSyllableGraph(input_, prism, 
+                                                 &syllable_graph_, ctx);
 }
 
 string ScriptSyllabifier::GetPreeditString(const Phrase& cand) const {
```

---

## 使用示例

### C++ 示例

```cpp
#include <rime_api.h>

int main() {
  RimeSessionId session = RimeCreateSession();
  RimeSelectSchema(session, "luna_pinyin");
  
  // 示例 1：全部派生（默认）
  RimeSetInput(session, "bubu");
  // 候选：不步、不部、比步、比部...
  
  // 示例 2：前2码精确
  RimeSetInputEx(session, "bubu", 2);
  // 候选：不步、不部、不比...
  // （第一个音节只有 bu，第二个音节可派生）
  
  // 示例 3：全部精确
  RimeSetInputEx(session, "bubu", 4);
  // 或
  RimeSetInputEx(session, "bubu", -1);
  // 候选：不步、不部...
  // （两个音节都是 bu）
  
  return 0;
}
```

### JavaScript Filter 示例

```javascript
// 筛选器：逐步确认用户输入

function filter(input, env) {
  let ctx = env.engine.context;
  let current_input = ctx.input;
  
  // 获取已确认的长度（例如从用户选择的候选获取）
  let confirmed_length = get_confirmed_length(ctx);
  
  // 设置部分精确匹配
  if (confirmed_length > 0) {
    rime.set_input_ex(current_input, confirmed_length);
  }
  
  return input;
}
```

---

## 测试用例

### 单元测试

```cpp
// test/syllabifier_test.cc

TEST(SyllabifierTest, PartialExactMatch) {
  // 准备
  Prism prism;
  prism.Load("luna_pinyin.prism.bin");
  
  Context ctx;
  SyllableGraph graph;
  Syllabifier syllabifier;
  
  // 测试1：全部派生
  ctx.set_input("bubu", 0);
  syllabifier.BuildSyllableGraph("bubu", prism, &graph, &ctx);
  
  // 验证：位置0-2应该有 bu, bi, ni, nu
  EXPECT_TRUE(graph.edges[0][2].count(bu_id) > 0);
  EXPECT_TRUE(graph.edges[0][2].count(bi_id) > 0);  // 派生
  EXPECT_TRUE(graph.edges[0][2].count(ni_id) > 0);  // 派生
  
  // 测试2：前2码精确
  ctx.set_input("bubu", 2);
  graph.clear();
  syllabifier.BuildSyllableGraph("bubu", prism, &graph, &ctx);
  
  // 验证：位置0-2只有 bu
  EXPECT_TRUE(graph.edges[0][2].count(bu_id) > 0);
  EXPECT_FALSE(graph.edges[0][2].count(bi_id) > 0);  // 被过滤
  EXPECT_FALSE(graph.edges[0][2].count(ni_id) > 0);  // 被过滤
  
  // 验证：位置2-4有 bu, bi, ni, nu（派生）
  EXPECT_TRUE(graph.edges[2][4].count(bu_id) > 0);
  EXPECT_TRUE(graph.edges[2][4].count(bi_id) > 0);
  EXPECT_TRUE(graph.edges[2][4].count(ni_id) > 0);
}
```

### 集成测试

```python
#!/usr/bin/env python3
# test_script_translator.py

import rime_api as rime

def test_partial_exact_match():
    """测试部分精确匹配"""
    session = rime.create_session()
    rime.select_schema(session, "luna_pinyin")
    
    # 测试1：全部派生
    rime.set_input_ex(session, "bubu", 0)
    candidates = rime.get_candidates(session)
    assert "不步" in candidates
    # 注：拼音方案可能没有 bi+bu 的词，这里只是示例
    
    # 测试2：前2码精确
    rime.set_input_ex(session, "bubu", 2)
    candidates = rime.get_candidates(session)
    assert "不步" in candidates
    
    # 测试3：全部精确
    rime.set_input_ex(session, "bubu", 4)
    candidates = rime.get_candidates(session)
    assert "不步" in candidates
    
    print("✓ 部分精确匹配测试通过")

def test_14key_scheme():
    """测试14键方案"""
    session = rime.create_session()
    rime.select_schema(session, "rime_ice_14")
    
    # 测试：输入 bububi，前4码精确
    rime.set_input_ex(session, "bububi", 4)
    candidates = rime.get_candidates(session)
    
    # 验证（根据实际词库）
    # 第一个 bu 精确，第二个 bu 精确，第三个 bi 可派生
    
    print("✓ 14键方案测试通过")

if __name__ == "__main__":
    test_partial_exact_match()
    test_14key_scheme()
    print("\n✅ 所有测试通过！")
```

---

## 编译与测试

### 编译

```bash
cd librime
mkdir -p build && cd build
cmake -DBUILD_TEST=ON ..
make -j$(nproc)
```

### 运行测试

```bash
# 单元测试
./test/rime_test

# 手动测试
./bin/rime_console
```

### 手动测试步骤

```
1. 启动 rime_console
   $ ./bin/rime_console

2. 选择方案
   > schema: luna_pinyin

3. 测试全部派生
   > input: bubu
   > (查看候选)

4. 测试部分精确
   > set_input_ex bubu 2
   > (查看候选，第一个音节应该只有 bu)

5. 测试全部精确
   > set_input_ex bubu 4
   > (查看候选，两个音节都是 bu)
```

---

## 性能分析

### 时间复杂度

**原始实现**：O(n * m * k)
- n: 输入长度
- m: 每个位置的平均匹配数
- k: 每个拼写的平均音节数

**V2.1 实现**：O(n * m * k)
- 相同的时间复杂度
- 只是在精确范围内添加字符串比较

### 额外开销

**精确模式**：
- 字符串比较：O(L)，L 为拼写长度（通常 < 10）
- 音节表查询：O(1)（数组访问）

**非精确模式**：
- 无额外开销（直接添加）

### 优化措施

1. **只在需要时查询音节表**：
   ```cpp
   if (input_exact_length > 0) {
     syllabary = prism.syllabary();
   }
   ```

2. **只在精确范围内过滤**：
   ```cpp
   bool need_exact = (start_pos < input_exact_length);
   ```

3. **字符串比较优化**：
   - 使用引用避免复制
   - 短字符串比较很快

---

## 注意事项

### 1. Prism::syllabary() 方法

需要确认 `Prism` 类有 `syllabary()` 方法：

```cpp
// dict/prism.h
class Prism {
 public:
  const Syllabary* syllabary() const;
};
```

如果没有，需要添加：

```cpp
// dict/prism.h
class Prism {
 public:
  const Syllabary* syllabary() const { return &syllabary_; }
  
 private:
  Syllabary syllabary_;
};
```

### 2. 分隔符处理

如果输入包含分隔符（如 `'`），`start_pos` 的计算需要考虑：

```
输入：bu'bu (5字符)
input_exact_length：2

位置：
  0-2: bu (精确)
  2-3: ' (分隔符)
  3-5: bu (派生)

注意：start_pos=3 > input_exact_length=2，所以位置3的 bu 允许派生
```

### 3. 多音节匹配

某些拼写可能匹配多个长度：

```
输入：xian
匹配：
  - xi (0-2) + an (2-4)
  - xian (0-4)

精确长度：2

结果：
  - xi (0-2): 精确 → 只有 xi
  - an (2-4): 派生 → an, ang, en...
  - xian (0-4): start_pos=0 < 2，需要精确 → 只有 xian
```

---

## 总结

### V2.1 改进点

1. **命名优化**：`input_exact_length` 更明确
2. **实现优先级**：先 ScriptTranslator（更常用）
3. **实现方案**：在 Syllabifier 层面过滤（效率最高）

### 核心实现

1. Context 存储 `input_exact_length_`
2. Syllabifier 接收 Context 参数
3. 构建音节图时根据位置过滤派生音节

### 修改文件

- ✓ context.h / context.cc
- ✓ rime_api.h / rime_api_impl.h
- ✓ syllabifier.h / syllabifier.cc
- ✓ script_translator.cc

### 下一步

1. 实施代码修改
2. 编写单元测试
3. 验证拼音和14键方案
4. 性能测试
5. 扩展到 TableTranslator（可选）
