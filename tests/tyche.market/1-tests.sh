tyche_market=tyche.mark32
mreg flon $tyche_market flonian
mtran flonian $tyche_market "100 FLON"
mset $tyche_market tyche.market
mcli set account permission $tyche_market active --add-code



mreg flon alice flonian
mreg flon bob flonian
mreg flon charlie flonian
mreg flon liquid flonian

mpush flon.mtoken transfer '["flontest","alice","100.00000000 ETH","deposit:1"]' -p flontest
mpush flon.mtoken transfer '["flontest","bob","50.00000000 ETH","deposit:1"]' -p flontest

mpush flon.mtoken transfer '["flonian","alice","50000.000000 USDT","deposit:1"]' -p flonian
mpush flon.mtoken transfer '["flonian","bob","50000.000000 USDT","deposit:1"]' -p flonian
mpush flon.mtoken transfer '["flonian","liquid","50000.000000 USDT","deposit:1"]' -p flonian



#初始化 Market
mpush $tyche_market init '["flonian"]' -p $tyche_market

#设置价格 TTL
mpush $tyche_market setpricettl '[600]' -p flonian
#上线 ETH 池（可抵押）
# mpush $tyche_market addreserve '[
#   {"sym":"8,ETH","contract":"flon.mtoken"},
#   7500,   # max_ltv = 75%
#   8000,   # liquidation_threshold = 80%
#   11000,  # liquidation_bonus = 10%
#   1000,   # reserve_factor = 10%
#   8000,   # U_opt = 80%
#   200,    # r0 = 2%
#   800,    # r_opt = 8%
#   3000    # r_max = 30%
# ]' -p flonian

mpush $tyche_market addreserve '[{"sym":"8,ETH","contract":"flon.mtoken"},7500,8000,11000,1000, 8000, 200,  800, 3000 ]' -p flonian

#上线 USDT 池（❌不作为抵押）

# mpush $tyche_market addreserve '[
#   {"sym":"6,USDT","contract":"flon.mtoken"},
#   1,      # max_ltv：>0 即可，被借开关
#   1,      # liquidation_threshold：≈0 抵押效果，但满足校验
#   10500,  # liquidation_bonus（无实际影响）
#   1000,   # reserve_factor
#   8000,   # u_opt
#   200,    # r0
#   600,    # r_opt
#   2000    # r_max
# ]' -p flonian

mpush $tyche_market addreserve '[{"sym":"6,USDT","contract":"flon.mtoken"},0, 0,10500, 1000,8000,200,600,2000 ]' -p flonian
#修改 USDT 池参数测试
#mpush $tyche_market setreserve '["USDT",0,0,10500,1000]' -p flonian

#喂价
mpush $tyche_market setprice '["ETH", "8000.000000 USDT"]' -p flonian
mpush $tyche_market setprice '["USDT", "1.000000 USDT"]' -p flonian

#存款（Supply）
##  Alice 存 ETH
##  mtran flonian bob "100 FLON"
mpush flon.mtoken transfer '["bob","'$tyche_market'","2.00000000 ETH","supply"]' -p bob


mpush flon.mtoken transfer '["alice","'$tyche_market'","20000.000000 USDT","supply"]' -p alice

# mpush flon.mtoken transfer '["'$tyche_market'","alice","2.00000000 ETH","supply"]' -p $tyche_market
# mpush flon.mtoken transfer '["'$tyche_market'","bob","20000.000000 USDT","supply"]' -p $tyche_market

#借款逻辑验证（重点）
## Charlie 无抵押直接借 USDT（应失败）
mpush $tyche_market borrow '["charlie","1000.000000 USDT"]' -p charlie

## Alice 用 ETH 借 USDT
mpush $tyche_market setcollat '["bob","ETH",true]' -p bob
#这个要失败
mpush $tyche_market setcollat '["alice","USDT",true]' -p alice


mpush $tyche_market borrow '["bob","2000.000000 USDT"]' -p bob

mpush $tyche_market borrow '["alice","500.000000 USDT"]' -p alice


# Bob 用 USDT 借 ETH（应失败，因为 USDT 池不可抵押）
mpush $tyche_market borrow '["bob","0.10000000 ETH"]' -p bob

#当前没有利息，无法提取
mpush $tyche_market claimint '["bob","USDT"]' -p bob

## Alice 借ETH
mpush $tyche_market borrow '["alice","0.06250000 ETH"]' -p alice





mpush flon.mtoken transfer '["bob","'$tyche_market'","4000.100000 USDT","repay:bob"]' -p bob