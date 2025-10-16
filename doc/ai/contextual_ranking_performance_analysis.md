# 上下文排序功能性能问题分析

## 问题描述

在连续快速输入多个字符时,出现明显卡顿现象。

---

## 性能瓶颈分析

### 1. **主要问题:每次输入都触发完整的重排序**

#### 当前实现流程:
```
用户输入一个字符
    ↓
生成候选词列表
    ↓
ContextualRankingFilter::Apply() 被调用
    ↓
收集前20个候选词 (max_candidates_)
    ↓
对每个候选词调用 Grammar::Query() 两次:
    - 左侧上下文评分: grammar_->Query(left_context, cand->text(), false)
    - 右侧上下文评分: grammar_->Query(cand->text(), right_context, true)
    ↓
排序所有候选词
    ↓
返回重排序后的结果
```

#### 性能消耗点:

**1) Grammar::Query() 调用次数过多**
- 每个候选词调用2次 (左侧+右侧)
- 20个候选词 = 40次 Query 调用
- 每次输入一个字符都会触发

**2) Octagram::Query() 内部开销大**
```cpp
// octagram.cc 第107-141行
double Octagram::Query(const string& context, const string& word, bool is_rear) {
  // 1. UTF-8字符串解析和编码
  string context_query = grammar::encode(
      last_n_unicode(context, n, context_len),
      str_end(context));
  string word_query = grammar::encode(
      str_begin(word),
      first_n_unicode(word, n, word_query_len));
  
  // 2. 多次数据库查询 (循环遍历上下文长度)
  for (const char* context_ptr = str_begin(context_query);
       context_len > 0;
       --context_len, context_ptr = grammar::next_unicode(context_ptr)) {
    int num_results = db_->Lookup(context_ptr, word_query, matches);
    // 处理查询结果...
  }
}
```

**开销来源:**
- UTF-8字符串解析和编码 (每次调用)
- 数据库查询 (GramDb::Lookup) - 磁盘I/O或内存查询
- 循环遍历不同长度的上下文组合

**3) 排序开销**
```cpp
// contextual_ranking_filter.cc 第113-115行
std::stable_sort(
    scored_candidates.begin(), scored_candidates.end(),
    [](const auto& a, const auto& b) { return a.second > b.second; });
```
- 每次输入都对20个候选词排序
- O(n log n) 复杂度

---

### 2. **次要问题:无缓存机制**

- 相同的上下文+候选词组合会被重复计算
- 例如输入"nihao"时:
  - "n" → 计算一次
  - "ni" → 重新计算 (可能有相同候选词)
  - "nih" → 再次重新计算
  - "niha" → 又重新计算
  - "nihao" → 还是重新计算

---

### 3. **触发频率过高**

每次按键都会触发:
```
按键 → 生成候选词 → Apply Filter → 重排序
```

快速输入5个字符 = 5次完整的重排序流程

---

## 性能数据估算

假设:
- 每次 Grammar::Query() 耗时: 1-2ms (包括数据库查询)
- 20个候选词 × 2次查询 = 40次调用
- 总耗时: 40-80ms

**快速输入场景:**
- 输入5个字符
- 总耗时: 200-400ms
- 用户感知: **明显卡顿**

---

## 解决方案

### 方案1: **延迟触发重排序** ⭐⭐⭐⭐⭐

**思路:** 不在每次按键时都重排序,而是等待用户停止输入后再触发。

**实现:**
```cpp
class ContextualRankingFilter : public Filter {
private:
  std::chrono::steady_clock::time_point last_input_time_;
  int debounce_delay_ms_ = 100;  // 延迟100ms
  
  an<Translation> Apply(an<Translation> translation, CandidateList* candidates) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_input_time_).count();
    
    // 如果距离上次输入小于延迟时间,跳过重排序
    if (elapsed < debounce_delay_ms_) {
      return translation;
    }
    
    // 执行重排序...
  }
};
```

**优点:**
- ✅ 大幅减少重排序次数
- ✅ 快速输入时不卡顿
- ✅ 实现简单

**缺点:**
- ⚠️ 需要异步机制或定时器

---

### 方案2: **减少重排序候选词数量** ⭐⭐⭐⭐

**思路:** 降低 `max_candidates_` 从20减少到5-10个。

**配置:**
```yaml
contextual_ranking_filter:
  max_rerank_candidates: 5  # 从20改为5
```

**效果:**
- 5个候选词 × 2次查询 = 10次调用
- 耗时: 10-20ms (减少75%)

**优点:**
- ✅ 立即生效,无需修改代码
- ✅ 显著降低耗时

**缺点:**
- ⚠️ 排序准确性可能下降

---

### 方案3: **添加查询缓存** ⭐⭐⭐⭐

**思路:** 缓存 Grammar::Query() 的结果。

**实现:**
```cpp
class ContextualRankingFilter : public Filter {
private:
  // 缓存: (left_context + word + right_context) → score
  std::unordered_map<string, double> query_cache_;
  int max_cache_size_ = 1000;
  
  double QueryWithCache(const string& left, const string& word, 
                        const string& right, bool is_rear) {
    string cache_key = left + "|" + word + "|" + right + "|" + 
                       (is_rear ? "1" : "0");
    
    auto it = query_cache_.find(cache_key);
    if (it != query_cache_.end()) {
      return it->second;  // 命中缓存
    }
    
    // 未命中,执行查询
    double score = grammar_->Query(left, word, is_rear);
    
    // 存入缓存 (限制大小)
    if (query_cache_.size() < max_cache_size_) {
      query_cache_[cache_key] = score;
    }
    
    return score;
  }
};
```

**优点:**
- ✅ 相同查询直接返回,速度极快
- ✅ 对连续输入效果显著

**缺点:**
- ⚠️ 增加内存占用
- ⚠️ 需要缓存失效策略

---

### 方案4: **异步重排序** ⭐⭐⭐

**思路:** 将重排序放到后台线程执行。

**实现:**
```cpp
class ContextualRankingFilter : public Filter {
private:
  std::thread ranking_thread_;
  std::atomic<bool> ranking_in_progress_{false};
  
  an<Translation> Apply(an<Translation> translation, CandidateList* candidates) {
    // 如果正在重排序,直接返回原始结果
    if (ranking_in_progress_) {
      return translation;
    }
    
    // 启动后台线程重排序
    ranking_in_progress_ = true;
    ranking_thread_ = std::thread([this, translation]() {
      // 执行重排序...
      ranking_in_progress_ = false;
    });
    
    return translation;  // 先返回原始结果
  }
};
```

**优点:**
- ✅ 不阻塞主线程
- ✅ 用户体验流畅

**缺点:**
- ⚠️ 实现复杂
- ⚠️ 需要线程安全机制
- ⚠️ 可能出现排序结果延迟显示

---

### 方案5: **智能触发策略** ⭐⭐⭐⭐⭐

**思路:** 根据输入状态决定是否触发重排序。

**规则:**
- 输入长度 < 2: 不重排序 (候选词太多,意义不大)
- 快速连续输入: 跳过重排序
- 停顿超过阈值: 触发重排序
- 候选词数量 < 5: 不重排序 (没必要)

**实现:**
```cpp
an<Translation> Apply(an<Translation> translation, CandidateList* candidates) {
  // 1. 检查输入长度
  if (engine_->context()->input().length() < 2) {
    return translation;
  }
  
  // 2. 检查候选词数量
  int cand_count = 0;
  auto temp_trans = translation;
  while (!temp_trans->exhausted() && cand_count < 5) {
    temp_trans->Next();
    ++cand_count;
  }
  if (cand_count < 5) {
    return translation;
  }
  
  // 3. 检查输入间隔
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - last_input_time_).count();
  last_input_time_ = now;
  
  if (elapsed < 100) {  // 100ms内的连续输入
    return translation;
  }
  
  // 执行重排序...
}
```

**优点:**
- ✅ 综合多种策略
- ✅ 平衡性能和准确性
- ✅ 用户体验最佳

---

## 推荐方案组合

### 🎯 **最佳实践: 方案2 + 方案5**

1. **立即调整配置** (方案2):
   ```yaml
   contextual_ranking_filter:
     max_rerank_candidates: 8  # 从20降到8
   ```

2. **添加智能触发** (方案5):
   - 输入长度 < 2: 跳过
   - 快速连续输入: 跳过
   - 候选词少: 跳过

**预期效果:**
- 耗时从 40-80ms 降到 8-16ms (减少80%)
- 快速输入时几乎不触发重排序
- 停顿时才进行精确排序

---

## 调试建议

### 1. 添加性能日志

```cpp
an<Translation> Apply(an<Translation> translation, CandidateList* candidates) {
  auto start = std::chrono::steady_clock::now();
  
  // 执行重排序...
  
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end - start).count();
  
  LOG(INFO) << "ContextualRanking took " << duration << "ms, "
            << "processed " << count << " candidates";
}
```

### 2. 监控 Grammar::Query 调用

```cpp
double left_score = 0.0;
if (!left_context.empty()) {
  auto start = std::chrono::steady_clock::now();
  left_score = grammar_->Query(left_context, cand->text(), false);
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();
  DLOG(INFO) << "Query took " << duration << "μs";
}
```

---

## 总结

**核心问题:** 每次按键都触发完整的重排序,且每个候选词需要2次数据库查询。

**解决思路:** 
1. 减少重排序频率 (延迟触发/智能跳过)
2. 减少每次处理的候选词数量
3. 添加缓存避免重复计算

**建议优先级:**
1. 🥇 立即调整 `max_rerank_candidates` 为 8
2. 🥈 实现智能触发策略 (跳过短输入和快速输入)
3. 🥉 添加性能日志,监控实际耗时
4. 考虑添加查询缓存 (如果问题仍存在)
