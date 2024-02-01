swap=tyche.s1
tnew $swap
tset $swap tyche.swap

user1=t.test1
tnew $user1


tcli set account permission $swap active --add-code



tcli push action amax.mtoken transfer '{"from": "ad", "to": "'$swap'", "quantity": "10000.0000 FFT", "memo": "deposit:2"}' -p ad
tcli push action amax.mtoken transfer '{"from": "ad", "to": "'$user1'", "quantity": "1000.000000 MUSDT", "memo": "deposit:2"}' -p ad


echo tcli push action amax.mtoken transfer '{"from": "'$user1'", "to": "'$swap'", "quantity": "100.000000 MUSDT", "memo": "deposit:2"}' -p $user1

tcli push action amax.mtoken transfer '{"from": "t.test1", "to": "tyche.s1", "quantity":"100.000000 MUSDT", "memo": "100.0000 FFT:100.0000 FFT"}' -p t.test1