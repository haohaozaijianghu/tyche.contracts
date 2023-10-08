
reward=tyche.r212
earn=tyche.s212
admin=admin2

#joss 的私钥 5J3SJ9LrrLaiWUDEHpTnJGVyY6B4P8eq7s6LDzNVgz9MURb2fhx
要建一个锁仓

tnew $reward
tnew $earn
tnew $admin
tset $earn tyche.earn
tset $reward tyche.reward
tcli set account permission $reward active --add-code
tcli set account permission $earn active --add-code

tcli push action $earn init '["'$admin'","'$reward'","joss",10, true]' -p $earn
tcli push action $reward init '["joss","'$earn'", true]' -p $reward

tcli push action $earn addrewardsym '{"sym":{ "sym":"6,TRUSD", "contract":"amax.mtoken" }}' -p $earn
tcli push action $earn addrewardsym '{"sym":{ "sym":"6,MUSDT", "contract":"amax.mtoken" }}' -p $earn
tcli push action $earn addrewardsym '{"sym":{ "sym":"6,BTCC", "contract":"mdao.token" }}' -p  $earn
tcli push action $earn addrewardsym '{"sym":{ "sym":"6,HSTZ", "contract":"mdao.token" }}' -p  $earn
tcli push action $earn addrewardsym '{"sym":{ "sym":"6,AMMX", "contract":"mdao.token" }}' -p  $earn


tcli push action $earn onshelfsym '{"sym":{ "sym":"6,MUSDT", "contract":"amax.mtoken" }, "on_shelf": true}' -p  $earn
tcli push action $earn onshelfsym '{"sym":{ "sym":"6,BTCC", "contract":"mdao.token" }, "on_shelf": true}' -p  $earn
tcli push action $earn onshelfsym '{"sym":{ "sym":"6,AMMX", "contract":"mdao.token" }, "on_shelf": true}' -p  $earn
//存入NUSDT
tcli push action amax.mtoken transfer '{"from": "joss", "to": "'$earn'", "quantity": "40000.000000 TRUSD", "memo": "deposit"}' -p joss


tcli push action amax.mtoken transfer '["ad", "joss", "3000.000000 TYCHE", "2"]' -p ad
tcli push action amax.mtoken transfer '["joss", "'$earn'", "100.000000 TYCHE", ""]' -p joss


tcli push action $earn createpool '[1, 1800, 1]' -p $earn
tcli push action $earn createpool '[2, 54000, 2]' -p $earn
tcli push action $earn createpool '[3, 162000, 3]' -p $earn
tcli push action $earn createpool '[4, 324000, 4]' -p $earn
tcli push action $earn createpool '[5, 648000, 5]' -p $earn

tcli push action amax.mtoken transfer '{"from": "joss", "to": "'$earn'", "quantity": "100.000000 MUSDT", "memo": "deposit:2"}' -p joss
tcli push action amax.mtoken transfer '{"from": "joss", "to": "'$reward'", "quantity": "1000.000000 MUSDT", "memo": "interest"}' -p joss



tcli get currency balance amax.mtoken $earn

tcli push action amax.mtoken transfer '{"from": "joss", "to": "josstest", "quantity": "1000.000000 MUSDT", "memo": "deposit:2"}' -p joss


tcli push action $reward splitintr '{}' -p joss

tcli push action $reward setrate '[ 5000 ]' -p $reward



tcli get currency balance amax.mtoken josstest

tcli get currency balance amax.mtoken tyche.s133
//存入MUSDT
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "'$earn'", "quantity": "100.000000 MUSDT", "memo": "deposit:2"}' -p josstest
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "'$earn'", "quantity": "100.000000 MUSDT", "memo": "deposit:1"}' -p josstest

//打回测试
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "'$earn'", "quantity": "100.000000 TRUSD", "memo": "1"}' -p josstest
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "'$earn'", "quantity": "100.000000 MUSDT", "memo": "deposit:2"}' -p josstest
//打入利息

tcli push action mdao.token transfer '{"from": "ad", "to": "joss", "quantity": "10000.000000 BTCC", "memo": "deposit:1"}' -p ad
tcli push action mdao.token transfer '{"from": "ad", "to": "joss", "quantity": "10000.000000 AMMX", "memo": "deposit:1"}' -p ad

tcli push action mdao.token transfer '{"from": "joss", "to": "'$reward'", "quantity": "100.000000 BTCC", "memo": "reward:0"}' -p joss
tcli push action mdao.token transfer '{"from": "joss", "to": "'$reward'", "quantity": "100.000000 AMMX", "memo": "reward:0"}' -p joss



tcli push action $earn claimreward '[ "joss", 2, "6,MUSDC" ]' -p joss
 tcli push action $earn claimreward '[ "joss", 2, "6,AMMX" ]' -p joss


tcli push action $reward splitintr '{}' -p joss
tcli get table $earn $earn earnpools
tcli get table $earn 2 earners

tcli push action  $earn  claimrewards '["josstest"]' -p josstest




tcli push action amax.mtoken transfer '{"from": "dex.user1", "to": "'$earn'", "quantity": "100.000000 TRUSD", "memo": "redeem:5"}' -p dex.user1


tcli push action amax.mtoken transfer '{"from": "joss", "to": "'$earn'", "quantity": "230.000000 TRUSD", "memo": "redeem:1"}' -p joss


tcli push action $reward initrwd '[ "230.000000 BTCC" ]' -p $reward