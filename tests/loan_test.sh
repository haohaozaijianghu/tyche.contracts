loan=tyche.l33
admin=ad
lp=ad
or=oracle3
user1=josstest
user2=josstest2

tnew $loan
tset $loan tyche.loan
tcli set account permission $loan active --add-code

proxy=proxy1
tnew $proxy
tset $proxy tyche.proxy
tcli set account permission $proxy active --add-code

earn=tyche.s312
tset $earn tyche.earn
tcli push action $earn initloaninfo '["'$proxy'"]' -p $earn
tcli push action $proxy init '["'$loan'", "'$earn'"]' -p $proxy

tcli push action $loan init '["'$admin'", "'$lp'","'$or'", "'$proxy'",true]' -p $loan
tcli get table $loan $loan global

tcli push action $loan addinteret '[800]' -p $admin

tcli push action $loan initinterest '[]' -p $loan

tcli push action $loan setcallatsym '[["8,MBTC", "amax.mtoken"], "btc"]' -p $admin
tcli push action $loan setcallatsym '[["8,METH", "amax.mtoken"], "eth"]' -p $admin
tcli push action $loan setcallatsym '[["8,AMAX", "amax.token"], "amax"]' -p $admin
tcli get table $loan $loan collsyms

tcli push action amax.mtoken transfer '{"from": "ad", "to": "'$loan'", "quantity": "20000.000000 MUSDT", "memo": "deposit"}' -p ad


# tcli push action amax.mtoken transfer '{"from": "ad", "to": "'$user1'", "quantity": "10.00000000 METH", "memo": "deposit"}' -p ad

tcli push action amax.mtoken transfer '{"from": "'$user1'", "to": "'$loan'", "quantity": "1.00000000 METH", "memo": "deposit"}' -p $user1
tcli get table $loan meth loaners

tcli push action $loan addinteret '[600]' -p $admin


tcli push action $loan onsubcallat '[ "'$user1'","0.10000000 METH"]' -p $user1

tcli push action $loan getmoreusdt '[ "'$user1'", "6,METH", "1000.000000 MUSDT"]' -p $user1
tcli push action $loan tgetprice '[ "6,METH"]' -p $user1
tcli push action $loan tgetliqrate '["'$user1'", "6,METH"]' -p $user1

#还款

tcli push action amax.mtoken transfer '{"from": "'$user1'", "to": "'$loan'", "quantity": "500.000000 MUSDT", "memo": "repay:6,METH"}' -p $user1


#清算
tcli push action amax.mtoken transfer '{"from": "'$user1'", "to": "'$loan'", "quantity": "500.000000 MUSDT", "memo": "liqudate:aabbccddee:6,METH"}' -p $user1


# tcli push action $loan addinteret '[600]' -p $admin
tcli get table $loan $loan interests

tcli push action $loan tgetinterest '["100.000000 MUSDT", 800, "2023-12-14T06:41:32", "2023-12-14T06:45:06"]' -p $user1

tcli push action $loan tgetinterest '["10000.000000 MUSDT", 800, "2033-12-11T06:44:06", "2023-12-14T06:45:06"]' -p $user1


tcli push action $loan forceliq '["'$user1'", "'$user1'", "6,METH"]' -p $user1



146/86400*8+30/86400*6



tcli push action amax.mtoken transfer '{"from": "terry", "to": "'$loan'", "quantity": "3.040000 MUSDT", "memo": "sendback:8,AMAX"}' -p terry



tcli push action amax.token transfer '{"from": "ad", "to": "1111", "quantity": "100.00000000 AMAX", "memo": "deposit"}' -p ad



tcli push action $loan sendtoearn '["100.000000 MUSDT"]' -p $loan
tcli get table $loan $loan global
tcli get table $proxy $proxy global
tcli get table $earn $earn global



