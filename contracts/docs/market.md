
# Tyche Market 需求概览（最新版）

> 说明：原先拆分的 Earn / Loan 产品形态已废弃，统一为 **Tyche Market** ——
> 一个 Aave-like 的多资产抵押借贷市场，所有资金行为均通过 **token transfer + memo** 驱动。

---

## 一、产品形态

- 链上多资产抵押借贷市场（Aave v3 风格）。
- 同一市场内支持：
  - 存款赚取利息（Supplier）
  - 抵押借款（Borrower）
  - 风险清算（Liquidator）
- 利率由利用率自动调节，风险失控时通过清算与应急机制收敛。

**设计原则**
- 不遍历用户
- 利息指数化、按需结算
- 所有资金流动必须先发生 transfer，再修改状态（EOSIO 安全模型）

---

## 二、核心参与者

- **Supplier（存款人）**
  - 向 Market 转入资产
  - 获得 supply shares
  - 持续获得利息收益
- **Borrower（借款人）**
  - 将已开启 collateral 的资产作为抵押
  - 借出其他资产
  - 必须始终满足健康因子约束
- **Liquidator（清算人）**
  - 在 HF < 1 时替用户还债
  - 获得抵押资产与清算奖励
- **Protocol（Tyche）**
  - 维护资产池状态
  - 管理利率、风险参数与应急模式
  - 累积 protocol reserve 作为系统缓冲

---

## 三、核心业务实体

### 1. Reserve（资产池）

每一种可借贷资产对应一个独立的 Reserve。

#### 资产属性
- token 合约
- symbol_code

#### 风控参数
- **max_ltv**：最大借款比例
- **liquidation_threshold**：清算阈值（HF 计算用）
- **liquidation_bonus**：基础清算奖励
- **reserve_factor**：协议抽成比例

#### 利率模型（v2）
- **U_opt**：最优利用率
- **r0 / r_opt / r_max**：分段借款利率曲线
- **max_rate_step_bp**：单次利率变化上限（防瞬变）
- **last_borrow_rate_bp**：当前生效利率锚点

#### 动态状态
- total_liquidity
- total_debt
- total_supply_shares
- total_borrow_shares
- protocol_reserve
- last_updated
- paused

---

### 2. User Position（用户仓位）

- 以 **用户 × Reserve** 为粒度
- 不做跨资产合并

字段：
- supply_shares
- borrow_shares
- collateral（是否作为抵押）

---

### 3. Global State（协议级）

- 全局暂停
- 价格 TTL
- 清算比例（close factor）
- emergency 模式与应急奖励
- backstop（协议储备下限）

---

## 四、完整生命周期

### 1️⃣ Reserve 上线（addreserve）

管理员创建资产池，需配置：

- max_ltv
- liquidation_threshold
- liquidation_bonus
- reserve_factor
- U_opt
- r0 / r_opt / r_max

**约束不变量**

liquidation_threshold × liquidation_bonus < 1

Reserve 可随时暂停，但不会清空历史仓位。

---

### 2️⃣ 存款（Supply）

触发方式：
```text
token.transfer(user → market, amount, "supply")

系统流程：
	1.	校验价格存在且未过期
	2.	推进 Reserve 计息
	3.	按当前比例换算 supply shares
	4.	更新用户仓位
	5.	默认开启 collateral

说明：
	•	存款 ≠ 借款
	•	是否作为抵押由 collateral 标志决定

⸻

3️⃣ 抵押开关（setcollat）
	•	用户可关闭抵押属性
	•	关闭后仍可继续赚取利息
	•	风险约束：关闭后必须满足 HF ≥ 1

禁止：
	•	对 supply_shares = 0 的仓位开启抵押

⸻

4️⃣ 借款（Borrow）

用户借出资产时，系统执行：
	1.	校验目标 Reserve 状态与价格
	2.	推进计息
	3.	校验可用流动性（含 buffer）
	4.	计算 borrow shares
	5.	模拟写入 → 重新估值
	6.	校验：
	•	debt ≤ max_borrowable_value
	•	HF ≥ 1
	7.	回滚模拟
	8.	实际转账 + 写入状态

原则：
	•	借款不会破坏系统整体安全
	•	利率随利用率动态变化

⸻

5️⃣ 利息模型（v2）
	•	利息不定时结算，仅在操作时推进
	•	利用率：

U = total_debt / total_liquidity

借款利率：
	•	U ≤ U_opt：线性上升
	•	U > U_opt：陡峭惩罚段

稳定机制：
	•	利率变动受 max_rate_step_bp 限制
	•	无债务时自动锚回 r0

利息分配：
	•	(1 - reserve_factor) → 存款人
	•	reserve_factor → protocol_reserve

⸻

6️⃣ 还款（Repay）

触发方式：

token.transfer(payer → market, amount, "repay:borrower")

流程：
	1.	推进计息
	2.	计算真实债务
	3.	自动 clamp（防多还）
	4.	扣减 borrow shares
	5.	回补池子流动性

支持：
	•	部分还款
	•	第三方代还

⸻

7️⃣ 提现（Withdraw）

用户提现时系统校验：
	1.	推进计息
	2.	计算可提最大金额
	3.	校验池子可用流动性
	4.	提现后 HF ≥ 1
	5.	转账并扣减 supply shares

⸻

8️⃣ 清算（Liquidation，v3）

触发方式（必须先转账）：

token.transfer(
  liquidator → market,
  repay_amount,
  "liquidate:borrower:DEBT:COLL"
)

清算前置条件
	•	borrower HF < 1
	•	抵押资产已开启 collateral

清算额度上限（三重约束）
	1.	清算人转入的 repay_amount
	2.	close_factor 限制
	3.	恢复 HF = 1 的理论上限

清算奖励
	•	基础奖励：liquidation_bonus
	•	emergency 模式下：
	•	额外奖励（有上限）
	•	要求 protocol_reserve ≥ backstop_min
	•	放宽价格 TTL

资金流顺序
	1.	liquidator 转入债务资产
	2.	market 释放抵押资产
	3.	更新 borrower / reserve 状态

⸻

五、应急模式（Emergency Mode）

启用后：
	•	清算奖励提高（受上限约束）
	•	价格 TTL 放宽
	•	启用 protocol reserve 作为系统 backstop

目标：
	•	极端行情下快速去杠杆
	•	鼓励清算，防止系统坏账

⸻

六、当前状态总结
	•	Tyche Market 已完整实现：
	•	多资产抵押借贷
	•	稳定利率模型（v2）
	•	严格清算决策树（v3）
	•	transfer-driven 的清算执行模型
	•	emergency 应急风控
