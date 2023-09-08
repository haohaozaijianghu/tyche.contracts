
reward=usdt.reward2
save=usdt.save2
admin=admin2

要建一个锁仓

tnew $reward
tnew $save
tnew $admin
tset $save usdt.save
tset $reward usdt.interest
tcli set account permission $reward active --add-code
tcli set account permission $save active --add-code

tcli push action $save init '["'$admin'","'$reward'","joss",10, true]' -p $save
tcli push action $reward init '["joss","'$save'", true]' -p $reward

tcli push action $save addrewardsym '{"sym":{ "sym":"6,TRUSD", "contract":"amax.mtoken" }, "interval": 86400}' -p $save
tcli push action $save addrewardsym '{"sym":{ "sym":"6,MUSDC", "contract":"amax.mtoken" }, "interval": 86400}' -p $save
tcli push action $save symonself '{"sym":{ "sym":"6,MUSDC", "contract":"amax.mtoken" }, "on_self": true}' -p  $save
//存入NUSDT
tcli push action amax.mtoken transfer '{"from": "joss", "to": "'$save'", "quantity": "1000.000000 TRUSD", "memo": "deposit"}' -p joss

tcli push action $save addsaveconf '[1, 10, 1]' -p $save
tcli push action $save addsaveconf '[2, 20, 2]' -p $save
tcli push action $save addsaveconf '[3, 60, 3]' -p $save
tcli push action $save addsaveconf '[4, 180, 4]' -p $save

tcli push action amax.mtoken transfer '{"from": "joss", "to": "$save", "quantity": "100.000000 MUSDT", "memo": "deposit:2"}' -p joss

tcli get currency balance amax.mtoken $save

tcli push action amax.mtoken transfer '{"from": "joss", "to": "josstest", "quantity": "1000.000000 MUSDT", "memo": "deposit:2"}' -p joss



tcli get currency balance amax.mtoken josstest
//存入MUSDT
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "usdt.save", "quantity": "100.000000 MUSDT", "memo": "deposit:2"}' -p josstest
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "usdt.save", "quantity": "100.000000 MUSDT", "memo": "deposit:1"}' -p josstest

//打回测试
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "usdt.save", "quantity": "100.000000 NUSDT", "memo": "1"}' -p josstest
tcli push action amax.mtoken transfer '{"from": "josstest", "to": "usdt.save", "quantity": "100.000000 MUSDT", "memo": "deposit:1"}' -p josstest
//打入利息

tcli push action amax.mtoken transfer '{"from": "ad", "to": "joss", "quantity": "10000.000000 MUSDC", "memo": "deposit:1"}' -p ad

tcli push action amax.mtoken transfer '{"from": "joss", "to": "usdt.intst", "quantity": "100.000000 MUSDC", "memo": ""}' -p joss

