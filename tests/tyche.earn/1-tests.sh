tyche_earn=tyche.earn11
mreg flon $tyche_earn flonian
mtran flonian $tyche_earn "100 FLON"
mset $tyche_earn tyche.earn
mcli set account permission $tyche_earn active --add-code




tyche_reward=tycreward111
mreg flon $tyche_reward flonian
mtran flonian $tyche_reward "100 FLON"
mset $tyche_reward tyche.reward
mcli set account permission $tyche_reward active --add-code


tyche_loan=tyche.loan11
mreg flon $tyche_loan flonian
mtran flonian $tyche_loan "100 FLON"
mset $tyche_loan tyche.loan
mcli set account permission $tyche_loan active --add-code


tyche_proxy=tycheproxy11
mreg flon $tyche_proxy flonian
mtran flonian $tyche_proxy "100 FLON"
mset $tyche_proxy tyche.proxy
mcli set account permission $tyche_proxy active --add-code

# 初始化 tyche_proxy 合约
mpush $tyche_proxy init '["tyche.loan11","tyche.earn11"]' -p $tyche_proxy

# 初始化 earn 合约
mpush tyche.earn11 init '["flonian","tycreward111","flonian", true]' -p tyche.earn11

# 初始化 reward 合约
mpush tycreward111 init '["flonian","tyche.earn11", true]' -p tycreward111

#添加奖励代币（可领的奖励币种）
mpush tyche.earn11 addrewardsym '{"sym":{ "sym":"6,USDT", "contract":"flon.mtoken" }}' -p tyche.earn11
mpush tyche.earn11 addrewardsym '{"sym":{ "sym":"6,TYCHE", "contract":"tyche.token" }}' -p tyche.earn11
mpush tyche.earn11 addrewardsym '{"sym":{ "sym":"6,TRUSD", "contract":"tyche.token" }}' -p tyche.earn11
mpush tyche.earn11 addrewardsym '{"sym":{ "sym":"8,FLON", "contract":"flon.token" }}' -p tyche.earn11

#设置最小存入金额
mpush tyche.earn11 setmindepamt '["1.000000 MUSDT"]' -p tyche.earn11

#设置存款池 (Pool)

mpush tyche.earn11 setpool '[1, 86400, 10]'   -p tyche.earn11   # 30秒，倍率1
mpush tyche.earn11 setpool '[2, 2592000, 15]'   -p tyche.earn11   # 60秒，倍率2
mpush tyche.earn11 setpool '[3, 7776000, 20]'  -p tyche.earn11   # 120秒，倍率3
mpush tyche.earn11 setpool '[4, 15552000, 30]'  -p tyche.earn11   # 120秒，倍率3
mpush tyche.earn11 setpool '[5, 31104000, 130]'  -p tyche.earn11   # 120秒，倍率3
mpush tyche.earn11 setpool '[6, 311040000, 130]'  -p tyche.earn11   # 120秒，倍率3
mpush tyche.earn11 setpool '[7, 311040000, 130]'  -p tyche.earn11   # 120秒，倍率3
mpush tyche.earn11 setpool '[8, 311040000, 130]'  -p tyche.earn11   # 120秒，倍率3

#初始化 loan 代理
mpush tyche.earn11 initloaninfo '["tycheproxy11"]' -p tyche.earn11


#向tyche.earn11 转入奖励币（合约操作）
mpush tyche.token transfer '["flonian","tyche.earn11","1000000.000000 TRUSD","1st issue"]' -p flonian

#存入本金（用户操作）
mpush flon.mtoken transfer '["flonian","tyche.earn11","10.000000 USDT","deposit:1"]' -p flonian


#打入奖励和利息（管理员操作）
mpush tyche.token transfer '["flonian","tycreward111","500.000000 TRUSD","reward:0"]' -p flonian    //0 把奖励金额 按池子存款权重（avl_principal × share_multiplier） 进行比例分配
mpush flon.mtoken transfer '["flonian","tycreward111","1000.000000 USDT","interest"]' -p flonian


#用户提取奖励/利息
mpush tyche.earn11 claimrewards '["flonian"]' -p flonian
mpush tyche.earn11 claimreward '["flonian","6,USDT"]' -p flonian

mpush tyche.token transfer '["flonian","tyche.earn11","100.000000 TRUSD","redeem:1"]' -p flonian   #用户取回本金（用户操作）


#loan初始化
mpush tyche.loan11 init '["flonian","flonian","price.oracle","tycheproxy11",true]' -p tyche.loan11

#设置支持的抵押物
mpush tyche.loan11 setcallatsym '[{"sym":"6,USDT","contract":"flon.mtoken"},"usdt"]' -p flonian
mpush tyche.loan11 setcallatsym '[{"sym":"8,ETH","contract":"flon.mtoken"},"eth"]' -p flonian
mpush tyche.loan11 setcallatsym '[{"sym":"8,BTC","contract":"flon.mtoken"},"btc"]' -p flonian
mpush tyche.loan11 setcallatsym '[{"sym":"8,BNB","contract":"flon.mtoken"},"bnb"]' -p flonian

#设置抵押率参数
#初始抵押率 = 150%
#清算抵押率 = 130%
#强平抵押率 = 110%
mpush tyche.loan11 setsymratio '["6,USDT", 15000, 13000, 11000]' -p flonian
#设置最小/最大抵押数量
mpush tyche.loan11 setcollquant '["8,BTC","0.00001000 BTC","1000.00000000 BTC"]' -p flonian
#设置利息（例如 10% 年化）
mpush tyche.loan11 addinterest '[1000]' -p flonian
#设置清算折价比例
mpush tyche.loan11 setliqpratio '[8700]' -p flonian

#添加抵押物（用户存 ETH）
mpush flon.mtoken transfer '["gahbnbehaskk","tyche.loan11","0.00001000 BTC","collateral"]' -p gahbnbehaskk
#动性提供者（lp_refueler 或 proxy 合约） 打 USDT 给 loan 合约。
mpush flon.mtoken transfer '["flonian","tyche.loan11","100.000000 USDT","loan fund"]' -p flonian

#借 USDT
mpush tyche.loan11 getmoreusdt '["gahbnbehaskk","8,BTC","0.020000 USDT"]' -p gahbnbehaskk
#偿还 USDT
mpush flon.mtoken transfer '["gahbnbehaskk","tyche.loan11","0.100000 USDT","sendback:8,BTC"]' -p gahbnbehaskk
#赎回抵押物
mpush tyche.loan11 onsubcallat '["gahbnbehaskk","0.00001000 BTC"]' -p gahbnbehaskk

#清算操作
#普通清算

mpush flon.mtoken transfer '["gahbnbehaskk","tyche.loan11","0.020000 USDT","liqbuy:8,BTC:gahbnbehaskk"]' -p gahbnbehaskk

#强制清算
mpush tyche.loan11 forceliq '["system","gahbnbehaskk","8,BTC"]' -p system
#Loan 把资金返还给 Earn
mpush tyche.loan11 sendtoearn '["1000.000000 USDT"]' -p tyche.loan11









#mpush flon.mtoken transfer '["flonian","gahbnbehaskk","2.00000000 BTC","deposit:1"]' -p flonian