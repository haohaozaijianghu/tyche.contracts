# tyche.contracts

## TycheFi Projects
Tyche 合约仓库。

- **Tyche Market**：统一的抵押借贷市场（原 Earn/Loan 模块合并）。
- 需求规格详见 [`docs/market.md`](docs/market.md)。

## 仓库/分支说明

- 当前工作分支：`work`。
- 远程仓库：`https://github.com/haohaozaijianghu/tyche.contracts`。
- 建议执行 `git fetch` 同步远程分支后再推送：
  - `git fetch origin`
  - `git push origin work`
- 合约源码路径：`contracts/tyche.market`（包含 CMake、头文件与实现）。

## 构建

```bash
./build.sh -m tyche.market
```

该命令会在 `build/contracts/tyche.market` 下生成合约产物，便于上传部署。