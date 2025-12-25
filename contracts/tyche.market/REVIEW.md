# Tyche Market 需求对照评审

根据 `contracts/docs/market.md` 描述的抵押借贷市场需求，对当前 `tyche.market` 合约实现进行了梳理：

## 已覆盖的核心流程
- 资产池上线：`addreserve` 允许管理员配置利率曲线、风控参数并支持暂停。【F:contracts/tyche.market/src/tyche.market.cpp†L49-L86】
- 存款/提现：`supply`、`withdraw` 在操作前推进利息、换算份额、校验流动性，并在提现时校验健康因子。【F:contracts/tyche.market/src/tyche.market.cpp†L88-L162】
- 抵押开关：`setcollat` 默认开启抵押，关闭时要求 HF ≥ 1。【F:contracts/tyche.market/src/tyche.market.cpp†L164-L183】
- 借款：`borrow` 推进利息、生成借款份额并检查 LTV 与健康因子后放款。【F:contracts/tyche.market/src/tyche.market.cpp†L185-L223】
- 还款与清算：`repay` 支持代还与部分还款，`liquidate` 在 HF<1 时按 50% close factor 清算并发放奖励。【F:contracts/tyche.market/src/tyche.market.cpp†L225-L347】
- 利息模型：利用率驱动的分段借款利率，按时间差累计利息并将供应方收益加入总流动性。【F:contracts/tyche.market/src/tyche.market.cpp†L356-L401】

## 与需求存在的主要差距/风险
1. **价格来源缺少外部喂价与新鲜度校验**：价格由管理员通过 `setprice` 直接写入本地表，未集成价格预言机或过期校验，生产环境下易受操纵，无法确保“按实时价格计算 HF”。【F:contracts/tyche.market/src/tyche.market.cpp†L29-L47】
2. **协议抽成未落地**：`_accrue` 只将 (1 - reserve_factor) 部分利息计入 `total_liquidity`，reserve_factor 对应的协议收益未记录或提取，协议收入被直接丢弃，无法满足“reserve_factor 作为协议抽成”的业务含义。【F:contracts/tyche.market/src/tyche.market.cpp†L356-L383】
3. **清算 close factor 固定为 50%**：需求中提到“限制最大清算比例（如 50%）”，但实现将上限写死为 50%，管理员无法调整，若后续希望根据风险策略调节会受限。【F:contracts/tyche.market/src/tyche.market.cpp†L300-L308】
4. **价格缺失时的前置校验覆盖不全**：`withdraw` 和 `borrow` 依赖 `_compute_valuation` 期间的价格存在性检查，但在资产池新增或价格被清空时，用户会在扣份额/增借款后才失败，导致多次无效修改尝试；可考虑在进入主要逻辑前先显式校验目标资产的喂价存在性，减少回滚成本。【F:contracts/tyche.market/src/tyche.market.cpp†L137-L153】【F:contracts/tyche.market/src/tyche.market.cpp†L185-L213】【F:contracts/tyche.market/src/tyche.market.cpp†L423-L468】

综上，核心流程基本覆盖需求，但上述缺口（尤其是价格可信度和协议抽成落地）需要补齐，才能满足生产场景下的安全性与运营性。
