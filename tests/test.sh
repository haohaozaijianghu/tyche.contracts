tcli set account permission usdt.intst active --add-code
tcli set account permission usdt.save active --add-code
 
 
tcli push action usdt.save addrewardsym '{"sym":{ "sym":"6,NUSDT", "contract":"amax.mtoken" }, "interval": 86400}' -p usdt.save
tcli push action usdt.save addrewardsym '{"sym":{ "sym":"6,MUSDC", "contract":"amax.mtoken" }, "interval": 86400}' -p usdt.save
tcli push action usdt.save symonself '{"sym":{ "sym":"6,MUSDC", "contract":"amax.mtoken" }, "on_self": true}' -p usdt.save
//存入NUSDT
tcli push action amax.mtoken transfer '{"from": "joss", "to": "usdt.save", "quantity": "1000.000000 NUSDT", "memo": "deposit"}' -p joss

tcli push action usdt.save addsaveconf '[1, 10, 1]' -p usdt.save
tcli push action usdt.save addsaveconf '[2, 20, 2]' -p usdt.save
tcli push action usdt.save addsaveconf '[3, 60, 3]' -p usdt.save
tcli push action usdt.save addsaveconf '[4, 180, 4]' -p usdt.save

tcli push action amax.mtoken transfer '{"from": "joss", "to": "usdt.save", "quantity": "100.000000 MUSDT", "memo": "deposit:2"}' -p joss

tcli get currency balance amax.mtoken usdt.save

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

