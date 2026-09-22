# LiteHTML 性能根因排查：批次 05

## 边界

只分析已有单轮数据与当前源码，不新增采样，不改生产算法。用户要求先证明真实问题、评估收益再修改；昂贵的约 1% 收益不值得。LiteHTMLView 是否已无空间不能先验确认，但此次排查重心为 LiteHTML。

数据：配对 LiteHTMLViewDemo/Saved/SinglePassAttribution/20260921-authorized-05/runs/{business,dashboard}/attribution.json 与原始 spans。时间是带诊断的墙钟区间，不是 CPU cycles。累计时间占比不是可兑现的端到端收益。

## 1. 已证实：细粒度文本更新会走全局 flow

调用链：business.html 的 100 ms 定时器更新三个库存文本 → document::set_inner_html 的纯文本分支初始化文本节点并设置 m_flow_layout_dirty → document::render_dirty 检查该标记直接调用 render → m_root_render->render 从根布局。

本轮 business 的 flow 为 616 次、2303.5123 ms，P95 4.6715 ms，占逻辑线程已覆盖 10107.641549 ms 的约 22.8%。纯文本更新仍保留格式父节点，但没有局部 flow 收敛机制。它是实质性的粗粒度失效路径；现有事件未记录每次 flow 的触发节点，不能把全部 616 次都归给库存定时器。

成本收益：假设能去掉一半 flow，节省约 1.15 秒累计逻辑工作（约 11.4% 已覆盖时间），这是条件估算，不是实测加速。直接建设通用增量布局需要处理文字宽度、换行、兄弟位置、容器尺寸、定位和滚动范围，成本高且风险大；目前不应实施。先确认可严格证明几何不变的更新占比；已有等尺寸数字更新路径受严格条件限制，不能靠删除 guard 提速，也不能假定本轮命中率。

## 2. 已证实：样式计算中反复进入外部文字测量

调用链：结构变动 → invalidate_structure_styles（设置 tree dirty，安排作用域匹配）→ rematch_styles → html_tag::compute_styles 递归子树 → el_text::compute_styles 无条件请求 container::text_width。即使文本、字体保持不变，该函数也未在这里比较测量输入后复用 m_size。

对原始逻辑线程事件按起点和反向终点排序、维护嵌套栈，统计 gdiRun 最近的非 measure 祖先：

|场景|全部 GDI run|compute 内 GDI run|compute 内 GDI 时间|
|---|---:|---:|---:|
|business|41633|41311|622.8531 ms|
|dashboard|147015|146938|2114.1727 ms|

business 有 14 次 compute 超过 10 ms，合计 628.4545 ms；dashboard 有 105 次，合计 2225.9592 ms。dashboard 105 次 treeCreate 与 105 次长 compute 在数量上吻合，结合定时增删事件行的代码，是结构变动放大样式/测量工作的强线索，但没有逐节点关联，不能宣称完全一一对应。

宿主 MeasureRunWidth 每次 CreateCompatibleDC/CreateFontW/GetTextExtentPoint32W 后销毁字体与 DC；上述墙钟时间覆盖整个函数，不能将它全算成 GDI 字形测量，亦不能全算成 LiteHTML 自身计算。当前记录没有文本/字体键、节点身份或 GDI 子阶段，所以尚不能算出重复测量命中率或资源复用的实际收益。

业务重建以 rows.innerHTML 替换整表，旧文本节点被删除。节点级测量复用不能跨新 DOM 节点生效；反之仪表盘仅增删事件行，旧节点有复用机会，但具体省多少仍未证实。禁止把加缓存当成已验证方案。文字测量总计业务 679.8 ms、仪表盘 2265.5 ms（约逻辑已覆盖时间 6.7%/17.7%），业务排序/删除/翻页的动作至 publish 区间 GDI 平均约 41–44 ms，可能对尾延迟比平均负载更有价值。

## 3. 排除低价值方向

- business selector matching 累计 26.8 ms、treeCreate 46.7 ms，各不足逻辑已覆盖时间 0.5%。不做高成本索引/建树重构。
- 解析与 DOM 创建/拆除合计 134.5 ms（约 1.33%）；不优先改 Gumbo 或换解析器。
- QuickJS 在 business 独占 103.77 ms（约 1.03%），不为业务页优先修改 QuickJS 内核。
- 不重启线程池探索或宽度返回值缓存；render 修改几何与行盒，不能只复用返回宽度。
- native draw 业务 2306.7 ms、仪表盘 3130.2 ms 包含宿主回调，现有粒度不足以认定 LiteHTML draw 内部是根因。

## 决策

本次不修改生产性能代码。重点保留两项：文本失效扩大到全局 flow；保留节点上的重复测量。前者总量足够大但正确性成本高，后者可能更小且直接影响长操作，但本轮缺少命中率证据。优先从现有源码和数据继续收窄，不能声称已有确定的两位数端到端收益。

如果后续必须新增计数或采样才能区分，则先报告缺口、提出单次限定方案，等待用户决定；本报告不授权新运行。后续实现必须有明确省掉的工作、字体/文本/布局失效边界、合理回归用例与端到端验收，不能以微基准 1% 或分配数量下降作为交付成果。
